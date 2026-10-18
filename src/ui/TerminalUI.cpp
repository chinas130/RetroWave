// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#include "ui/TerminalUI.h"

#include "ui/views/CoverArtView.h"
#include "ui/views/ModalsView.h"
#include "ui/views/ViewUtils.h"

#include <ncurses.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#ifndef BUTTON5_PRESSED
#if defined(NCURSES_MOUSE_VERSION) && NCURSES_MOUSE_VERSION > 1
#define BUTTON5_PRESSED NCURSES_MOUSE_MASK(5, NCURSES_BUTTON_PRESSED)
#else
#define BUTTON5_PRESSED 0
#endif
#endif

namespace retrowave {

TerminalUI::TerminalUI(PlaybackEngine& engine, const volatile std::sig_atomic_t* shutdownSignal)
    : engine_(engine), shutdownSignal_(shutdownSignal) {
    settings_ = settingsStore_.load();
    engine_.setVolume(settings_.volume);
    engine_.setRepeatMode(settings_.repeatMode);
    engine_.setShuffleMode(settings_.shuffleMode);
    
    if (settings_.layoutPreset == "visualizer") layoutPreset_ = LayoutPreset::VisualizerFocus;
    else if (settings_.layoutPreset == "lyrics") layoutPreset_ = LayoutPreset::LyricsFocus;
    else if (settings_.layoutPreset == "compact") layoutPreset_ = LayoutPreset::Compact;
    else layoutPreset_ = LayoutPreset::Default;
    
    keyBinder_.loadFromToml(settings_.keysTable);

    std::filesystem::path configDir;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && xdg[0] != '\0') {
        configDir = std::filesystem::path(xdg) / "retrowave";
    } else if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        configDir = std::filesystem::path(home) / ".config" / "retrowave";
    }
    if (!configDir.empty()) {
        themeManager_.loadCustomThemes(configDir / "themes");
    }
    themeManager_.selectTheme(settings_.theme);

    mediaSession_.setOnPlayPause([this]() { engine_.togglePause(); });
    mediaSession_.setOnNext([this]() {
        engine_.next();
        selectedIndex_ = engine_.snapshot().currentIndex;
    });
    mediaSession_.setOnPrevious([this]() {
        engine_.previous();
        selectedIndex_ = engine_.snapshot().currentIndex;
    });
}

TerminalUI::~TerminalUI() {
    flushSettingsFromEngine();
}

int TerminalUI::run() {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    mousemask(BUTTON1_CLICKED | BUTTON4_PRESSED | BUTTON5_PRESSED, nullptr);
    mouseinterval(0);
    if (has_colors()) {
        start_color();
        use_default_colors();
        themeManager_.applyToNcurses();
    }

    while (running_ && !shutdownRequested()) {
        engine_.update();
        if (detailMode_ == DetailMode::Lyrics) {
            engine_.ensureLyrics();
        }
        PlaybackSnapshot snapshot = engine_.snapshot();
        syncErrorOverlay(snapshot);

        const auto beforeInput = std::chrono::steady_clock::now();
        timeout(computePollTimeout(snapshot, beforeInput));
        const int key = getch();
        if (key != ERR) {
            handleInput(key);
        }

        engine_.update();
        snapshot = engine_.snapshot();
        syncErrorOverlay(snapshot);
        syncMediaSession(snapshot);
        mediaSession_.pumpEvents();

        if (snapshot.hasTrack && snapshot.currentIndex != lastCurrentIndex_) {
            selectedIndex_ = snapshot.currentIndex;
        }
        draw(snapshot, std::chrono::steady_clock::now());
    }

    flushSettingsFromEngine();
    endwin();
    return 0;
}

int TerminalUI::computePollTimeout(
    const PlaybackSnapshot& snapshot,
    std::chrono::steady_clock::time_point now) const {
    using namespace std::chrono;

    if (settingsOpen_) {
        return 1000;
    }

    if (!activeError_.empty() || !modalBody_.empty()) {
        return 180;
    }

    if (!snapshot.hasTrack || snapshot.paused || snapshot.loading) {
        return 180;
    }

    if (detailMode_ == DetailMode::Visualizer) {
        constexpr auto kVisualizerInterval = milliseconds(80);
        if (lastVisualizerRedraw_.time_since_epoch().count() == 0) {
            return 0;
        }

        const auto nextTick = lastVisualizerRedraw_ + kVisualizerInterval;
        if (nextTick <= now) {
            return 0;
        }
        return static_cast<int>(duration_cast<milliseconds>(nextTick - now).count());
    }

    return 140;
}

void TerminalUI::clearRect(const Rect& rect) const {
    for (int row = 0; row < rect.height; ++row) {
        mvhline(rect.top + row, rect.left, ' ', rect.width);
    }
}

void TerminalUI::drawHeader(const Rect& rect, const PlaybackSnapshot& snapshot) const {
    mvhline(rect.top, rect.left, ' ', rect.width);
    if (has_colors()) attron(COLOR_PAIR(static_cast<int>(ColorToken::HeaderTitle)) | A_BOLD);
    else attron(A_BOLD);
    mvprintw(rect.top, rect.left + 2, "RetroWave");
    if (has_colors()) attroff(COLOR_PAIR(static_cast<int>(ColorToken::HeaderTitle)) | A_BOLD);
    else attroff(A_BOLD);
    
    if (has_colors()) attron(COLOR_PAIR(static_cast<int>(ColorToken::HeaderText)));
    mvprintw(rect.top, rect.left + 14, "Terminal player with ASCII album art and .lrc lyrics");
    if (has_colors()) attroff(COLOR_PAIR(static_cast<int>(ColorToken::HeaderText)));

    const std::string shuffleText = std::string("Shuffle: ") + shuffleModeLabel(snapshot.shuffleMode);
    const std::string repeatText = std::string("Repeat: ") + repeatModeLabel(snapshot.repeatMode);
    const int shuffleLeft = rect.left + rect.width - static_cast<int>(shuffleText.size()) - 2;
    const int repeatLeft = shuffleLeft - static_cast<int>(repeatText.size()) - 3;

    if (repeatLeft > rect.left + 54) {
        attron(A_BOLD);
        mvprintw(rect.top, repeatLeft, "%s", repeatText.c_str());
        attroff(A_BOLD);
    }

    if (shuffleLeft > rect.left + 54) {
        attron(A_BOLD);
        mvprintw(rect.top, shuffleLeft, "%s", shuffleText.c_str());
        attroff(A_BOLD);
    }
}

void TerminalUI::draw(const PlaybackSnapshot& snapshot, std::chrono::steady_clock::time_point now) {
    int rows = 0;
    int cols = 0;
    getmaxyx(stdscr, rows, cols);

    const int minWidth = 48;
    const int minHeight = 10;
    if (rows < minHeight || cols < minWidth) {
        erase();
        mvprintw(1, 2, "RetroWave needs at least %dx%d terminal size.", minWidth, minHeight);
        mvprintw(3, 2, "Current size: %dx%d", cols, rows);
        mvprintw(5, 2, "Resize the terminal to continue.");
        wnoutrefresh(stdscr);
        doupdate();
        layoutValid_ = false;
        needsFullRedraw_ = true;
        overlayVisibleLastFrame_ = false;
        return;
    }

    const bool overlayVisible = !activeError_.empty() || settingsOpen_ || !modalBody_.empty();
    if (!overlayVisible && overlayVisibleLastFrame_) {
        needsFullRedraw_ = true;
    }
    overlayVisibleLastFrame_ = overlayVisible;

    if (overlayVisible) {
        if (settingsOpen_) {
            wnoutrefresh(stdscr);
            ModalsView::drawSettingsOverlay(rows, cols, settingsSelectedIndex_, settings_, layoutPreset_, themeManager_.current());
            doupdate();
            return;
        }

        erase();
        if (!activeError_.empty()) {
            ModalsView::drawModalOverlay(rows, cols, "Error", "Playback backend or decode failure", activeError_);
        } else {
            ModalsView::drawLegalScreen(rows, cols, modalTitle_, modalSubtitle_, modalBody_);
        }
        wnoutrefresh(stdscr);
        doupdate();
        return;
    }

    const Layout nextLayout = LayoutEngine::computeLayout(rows, cols, layoutPreset_);
    if (!layoutValid_ ||
        nextLayout.rows != layout_.rows ||
        nextLayout.cols != layout_.cols ||
        nextLayout.playlistFrame.width != layout_.playlistFrame.width ||
        nextLayout.albumFrame.height != layout_.albumFrame.height ||
        nextLayout.detailFrame.height != layout_.detailFrame.height) {
        layout_ = nextLayout;
        layoutValid_ = true;
        needsFullRedraw_ = true;
        visualizerView_.clearState();
    } else {
        layout_ = nextLayout;
    }

    const bool fullRedraw = needsFullRedraw_;
    const bool headerDirty = fullRedraw || snapshot.repeatMode != lastRepeatMode_ ||
        snapshot.shuffleMode != lastShuffleMode_;
    const bool playlistDirty =
        fullRedraw || selectedIndex_ != lastSelectedIndex_ || snapshot.currentIndex != lastCurrentIndex_ ||
        snapshot.hasTrack != lastHasTrack_;
    const bool coverDirty = fullRedraw || snapshot.path != lastTrackPath_;
    const bool metaDirty =
        fullRedraw || snapshot.path != lastTrackPath_ || snapshot.title != lastTitle_ || snapshot.artist != lastArtist_ ||
        snapshot.album != lastAlbum_ || snapshot.state != lastPlaybackState_ ||
        (snapshot.lyrics && snapshot.lyrics->found) != lastLyricsFound_ ||
        static_cast<int>(std::round(snapshot.volume * 100.0F)) != lastVolumePercent_ ||
        snapshot.repeatMode != lastRepeatMode_ || snapshot.shuffleMode != lastShuffleMode_;
    const bool timeDirty =
        fullRedraw || snapshot.path != lastTrackPath_ ||
        static_cast<int>(std::round(snapshot.positionSeconds)) != lastPositionSecond_;
    const bool detailFrameDirty = fullRedraw || detailMode_ != lastDetailMode_;

    bool detailDirty = fullRedraw || detailMode_ != lastDetailMode_;
    if (detailMode_ == DetailMode::Visualizer) {
        const bool due = fullRedraw || snapshot.path != lastTrackPath_ ||
            lastVisualizerRedraw_.time_since_epoch().count() == 0 ||
            now - lastVisualizerRedraw_ >= std::chrono::milliseconds(80);
        detailDirty = detailDirty || due;
        if (detailDirty) {
            lastVisualizerRedraw_ = now;
        }
    } else {
        int activeLyricIndex = -1;
        if (snapshot.lyrics && snapshot.lyrics->timed) {
            for (std::size_t index = 0; index < snapshot.lyrics->lines.size(); ++index) {
                if (snapshot.lyrics->lines[index].timestampSeconds <= snapshot.positionSeconds) {
                    activeLyricIndex = static_cast<int>(index);
                } else {
                    break;
                }
            }
        }

        bool timedRedrawDue = false;
        if (snapshot.lyrics && snapshot.lyrics->timed && activeLyricIndex >= 0) {
            timedRedrawDue = lastVisualizerRedraw_.time_since_epoch().count() == 0 ||
                             now - lastVisualizerRedraw_ >= std::chrono::milliseconds(100);
        }

        detailDirty = detailDirty || snapshot.path != lastTrackPath_ || activeLyricIndex != lastActiveLyricIndex_ || timedRedrawDue;
        lastActiveLyricIndex_ = activeLyricIndex;
        if (detailDirty) {
            lastVisualizerRedraw_ = now;
        }
    }

    if (fullRedraw) {
        erase();
        drawHeader(layout_.header, snapshot);
        drawFrame(
            layout_.playlistFrame.top,
            layout_.playlistFrame.left,
            layout_.playlistFrame.height,
            layout_.playlistFrame.width,
            "Playlist");
        drawFrame(
            layout_.albumFrame.top,
            layout_.albumFrame.left,
            layout_.albumFrame.height,
            layout_.albumFrame.width,
            "Album Card");
        drawFrame(
            layout_.detailFrame.top,
            layout_.detailFrame.left,
            layout_.detailFrame.height,
            layout_.detailFrame.width,
            detailMode_ == DetailMode::Lyrics ? "Lyrics" : "Visualizer");
    } else if (detailFrameDirty) {
        clearRect(layout_.detailFrame);
        drawFrame(
            layout_.detailFrame.top,
            layout_.detailFrame.left,
            layout_.detailFrame.height,
            layout_.detailFrame.width,
            detailMode_ == DetailMode::Lyrics ? "Lyrics" : "Visualizer");
    }

    if (headerDirty && !fullRedraw) {
        drawHeader(layout_.header, snapshot);
    }

    if (playlistDirty) {
        clearRect(layout_.playlistContent);
        drawPlaylist(
            layout_.playlistContent.top,
            layout_.playlistContent.left,
            layout_.playlistContent.height,
            layout_.playlistContent.width,
            snapshot);
    }

    if (coverDirty) {
        clearRect(layout_.cover);
        drawCoverPane(layout_.cover.top, layout_.cover.left, layout_.cover.height, layout_.cover.width, snapshot);
    }

    if (metaDirty) {
        clearRect(layout_.meta);
        drawMetaPane(layout_.meta.top, layout_.meta.left, layout_.meta.height, layout_.meta.width, snapshot);
    }

    if (timeDirty) {
        clearRect(layout_.time);
        drawTimePane(layout_.time.top, layout_.time.left, layout_.time.height, layout_.time.width, snapshot);
    }

    if (detailDirty) {
        clearRect(layout_.detailContent);
        if (detailMode_ == DetailMode::Lyrics) {
            lyricsView_.render(layout_.detailContent, snapshot, layoutPreset_, themeManager_.current());
        } else {
            visualizerView_.render(layout_.detailContent, snapshot, themeManager_.current());
        }
    }

    if (!activeError_.empty()) {
        ModalsView::drawModalOverlay(rows, cols, "Error", "Playback backend or decode failure", activeError_);
    }

    wnoutrefresh(stdscr);
    doupdate();

    needsFullRedraw_ = false;
    lastSelectedIndex_ = selectedIndex_;
    lastCurrentIndex_ = snapshot.currentIndex;
    lastTrackPath_ = snapshot.path;
    lastTitle_ = snapshot.title;
    lastArtist_ = snapshot.artist;
    lastAlbum_ = snapshot.album;
    lastHasTrack_ = snapshot.hasTrack;
    lastPlaybackState_ = snapshot.state;
    lastLyricsFound_ = snapshot.lyrics && snapshot.lyrics->found;
    lastVolumePercent_ = static_cast<int>(std::round(snapshot.volume * 100.0F));
    lastRepeatMode_ = snapshot.repeatMode;
    lastShuffleMode_ = snapshot.shuffleMode;
    lastPositionSecond_ = static_cast<int>(std::round(snapshot.positionSeconds));
    lastDetailMode_ = detailMode_;
}

void TerminalUI::drawFrame(int top, int left, int height, int width, const char* title) const {
    if (width <= 0 || height <= 0) return;
    const auto& theme = themeManager_.current();
    if (has_colors()) {
        attron(COLOR_PAIR(static_cast<int>(ColorToken::Border)));
    }

    if (theme.borderStyle == BorderStyle::Ascii) {
        mvaddch(top, left, '+');
        mvhline(top, left + 1, '-', width - 2);
        mvaddch(top, left + width - 1, '+');
        mvvline(top + 1, left, '|', height - 2);
        mvvline(top + 1, left + width - 1, '|', height - 2);
        mvaddch(top + height - 1, left, '+');
        mvhline(top + height - 1, left + 1, '-', width - 2);
        mvaddch(top + height - 1, left + width - 1, '+');
    } else if (theme.borderStyle == BorderStyle::Rounded) {
        mvprintw(top, left, "╭");
        for (int i = 1; i < width - 1; ++i) mvprintw(top, left + i, "─");
        mvprintw(top, left + width - 1, "╮");
        for (int i = 1; i < height - 1; ++i) {
            mvprintw(top + i, left, "│");
            mvprintw(top + i, left + width - 1, "│");
        }
        mvprintw(top + height - 1, left, "╰");
        for (int i = 1; i < width - 1; ++i) mvprintw(top + height - 1, left + i, "─");
        mvprintw(top + height - 1, left + width - 1, "╯");
    } else if (theme.borderStyle == BorderStyle::Double) {
        mvprintw(top, left, "╔");
        for (int i = 1; i < width - 1; ++i) mvprintw(top, left + i, "═");
        mvprintw(top, left + width - 1, "╗");
        for (int i = 1; i < height - 1; ++i) {
            mvprintw(top + i, left, "║");
            mvprintw(top + i, left + width - 1, "║");
        }
        mvprintw(top + height - 1, left, "╚");
        for (int i = 1; i < width - 1; ++i) mvprintw(top + height - 1, left + i, "═");
        mvprintw(top + height - 1, left + width - 1, "╝");
    } else {
        mvaddch(top, left, ACS_ULCORNER);
        mvhline(top, left + 1, ACS_HLINE, width - 2);
        mvaddch(top, left + width - 1, ACS_URCORNER);
        mvvline(top + 1, left, ACS_VLINE, height - 2);
        mvvline(top + 1, left + width - 1, ACS_VLINE, height - 2);
        mvaddch(top + height - 1, left, ACS_LLCORNER);
        mvhline(top + height - 1, left + 1, ACS_HLINE, width - 2);
        mvaddch(top + height - 1, left + width - 1, ACS_LRCORNER);
    }

    if (has_colors()) {
        attroff(COLOR_PAIR(static_cast<int>(ColorToken::Border)));
        attron(COLOR_PAIR(static_cast<int>(ColorToken::HeaderTitle)) | A_BOLD);
    } else {
        attron(A_BOLD);
    }
    
    mvprintw(top, left + 2, " %s ", title);
    
    if (has_colors()) {
        attroff(COLOR_PAIR(static_cast<int>(ColorToken::HeaderTitle)) | A_BOLD);
    } else {
        attroff(A_BOLD);
    }
}

void TerminalUI::drawPlaylist(int top, int left, int height, int width, const PlaybackSnapshot& snapshot) const {
    if (width <= 0 || height <= 0) return;
    const auto& playlist = engine_.playlist();
    if (playlist.empty()) {
        mvprintw(top, left, "No tracks.");
        return;
    }

    const int visibleRows = std::max(1, height - 1);
    int firstVisible = 0;
    if (selectedIndex_ >= static_cast<std::size_t>(visibleRows)) {
        firstVisible = static_cast<int>(selectedIndex_) - visibleRows + 1;
    }

    for (int row = 0; row < visibleRows; ++row) {
        const std::size_t index = static_cast<std::size_t>(firstVisible + row);
        if (index >= playlist.size()) {
            break;
        }

        const bool isSelected = index == selectedIndex_;
        const bool isCurrent = snapshot.hasTrack && index == snapshot.currentIndex;
        const std::string prefix = isCurrent ? "> " : "  ";
        const std::string line = prefix + trimText(playlist.titleAt(index), width - 4);

        int colorToken = static_cast<int>(ColorToken::TrackNormal);
        if (isCurrent && isSelected) colorToken = static_cast<int>(ColorToken::TrackPlayingSelected);
        else if (isCurrent) colorToken = static_cast<int>(ColorToken::TrackPlaying);
        else if (isSelected) colorToken = static_cast<int>(ColorToken::TrackSelected);

        if (isSelected) {
            attron(A_REVERSE);
        }
        if (has_colors()) {
            attron(COLOR_PAIR(colorToken));
            if (isCurrent) attron(A_BOLD);
        }

        drawTextLine(top + row, left, width, line);

        if (has_colors()) {
            if (isCurrent) attroff(A_BOLD);
            attroff(COLOR_PAIR(colorToken));
        }
        if (isSelected) {
            attroff(A_REVERSE);
        }
    }
}

void TerminalUI::drawAlbumCard(int top, int left, int height, int width, const PlaybackSnapshot& snapshot) const {
    if (!snapshot.hasTrack) {
        mvprintw(top, left, "No track loaded.");
        return;
    }

    const int artWidth = std::clamp(width / 3, 18, 28);
    const int infoLeft = left + artWidth + 2;
    const int infoWidth = std::max(12, width - artWidth - 2);
    const int timeHeight = height >= 6 ? 2 : 1;
    const int gapHeight = height >= 8 ? 1 : 0;
    const int metaHeight = std::max(1, height - timeHeight - gapHeight);
    const int timeTop = top + metaHeight + gapHeight;

    drawCoverPane(top, left, height, artWidth, snapshot);
    drawMetaPane(top, infoLeft, metaHeight, infoWidth, snapshot);
    drawTimePane(timeTop, infoLeft, timeHeight, infoWidth, snapshot);
}

void TerminalUI::drawCoverPane(int top, int left, int height, int width, const PlaybackSnapshot& snapshot) const {
    if (width <= 0 || height <= 0) return;
    const auto artLines = CoverArtView::renderCoverArt(snapshot.albumArt, width, height, settings_.coverArtMode);
    const int artTop = top + std::max(0, (height - static_cast<int>(artLines.size())) / 2);

    for (int row = 0; row < height; ++row) {
        mvhline(top + row, left, ' ', width);
    }

    for (int row = 0; row < std::min(height, static_cast<int>(artLines.size())); ++row) {
        const auto& line = artLines[static_cast<std::size_t>(row)];
        const int artLeft = left + std::max(0, (width - static_cast<int>(line.size())) / 2);
        if (has_colors()) {
            attron(COLOR_PAIR(4));
        }
        mvprintw(artTop + row, artLeft, "%s", line.c_str());
        if (has_colors()) {
            attroff(COLOR_PAIR(4));
        }
    }
}

void TerminalUI::drawMetaPane(int top, int left, int height, int width, const PlaybackSnapshot& snapshot) const {
    if (height <= 0 || width <= 0) {
        return;
    }

    const std::string state = playbackStateLabel(snapshot.state);
    const std::string lyricsStatus = snapshot.lyrics && snapshot.lyrics->found ? "loaded" : "missing";

    std::vector<std::string> lines;
    lines.reserve(9);
    lines.push_back(trimText(snapshot.title, width));
    lines.push_back({});
    lines.push_back("Artist : " + trimText(snapshot.artist.empty() ? "Unknown" : snapshot.artist, std::max(0, width - 9)));
    lines.push_back("Album  : " + trimText(snapshot.album.empty() ? "Unknown" : snapshot.album, std::max(0, width - 9)));
    lines.push_back(std::string("Source : ") + (snapshot.remote ? (snapshot.live ? "stream" : "online") : "local"));
    lines.push_back("State  : " + trimText(state, std::max(0, width - 9)));
    lines.push_back("Volume : " + std::to_string(static_cast<int>(std::round(snapshot.volume * 100.0F))) + "%");
    lines.push_back(std::string("Repeat : ") + std::string(repeatModeLabel(snapshot.repeatMode)));
    lines.push_back(std::string("Shuffle: ") + std::string(shuffleModeLabel(snapshot.shuffleMode)));
    lines.push_back("Lyrics : " + lyricsStatus);

    for (int row = 0; row < height; ++row) {
        mvhline(top + row, left, ' ', width);
    }

    for (int row = 0; row < std::min(height, static_cast<int>(lines.size())); ++row) {
        if (row == 0) {
            if (has_colors()) attron(COLOR_PAIR(static_cast<int>(ColorToken::MetaTitle)) | A_BOLD);
            else attron(A_BOLD);
        } else {
            if (has_colors()) attron(COLOR_PAIR(static_cast<int>(ColorToken::MetaValue)));
        }
        drawTextLine(top + row, left, width, lines[static_cast<std::size_t>(row)]);
        if (row == 0) {
            if (has_colors()) attroff(COLOR_PAIR(static_cast<int>(ColorToken::MetaTitle)) | A_BOLD);
            else attroff(A_BOLD);
        } else {
            if (has_colors()) attroff(COLOR_PAIR(static_cast<int>(ColorToken::MetaValue)));
        }
    }

    if (height >= 2) {
        drawTextLine(top + height - 1, left, width, "Source : " + snapshot.path);
    }
}

void TerminalUI::drawTimePane(int top, int left, int height, int width, const PlaybackSnapshot& snapshot) const {
    if (height <= 0 || width <= 0) {
        return;
    }

    for (int row = 0; row < height; ++row) {
        mvhline(top + row, left, ' ', width);
    }

    if (height >= 4) {
        attron(A_DIM);
        drawTextLine(top, left, width, "Time");
        attroff(A_DIM);

        const int barWidth = std::max(4, width - 2);
        if (snapshot.hasTrack && snapshot.durationSeconds > 0.0 && !snapshot.live) {
            const double fraction = std::clamp(snapshot.positionSeconds / snapshot.durationSeconds, 0.0, 1.0);
            const int filled = static_cast<int>(std::round(fraction * static_cast<double>(barWidth)));
            
            if (has_colors()) attron(COLOR_PAIR(static_cast<int>(ColorToken::TimeBarEmpty)));
            mvaddch(top + 1, left, '[');
            mvaddch(top + 1, left + barWidth + 1, ']');
            if (has_colors()) attroff(COLOR_PAIR(static_cast<int>(ColorToken::TimeBarEmpty)));
            for (int i = 0; i < barWidth; ++i) {
                if (i < filled) {
                    if (has_colors()) attron(COLOR_PAIR(static_cast<int>(ColorToken::TimeBarFilled)));
                    mvaddch(top + 1, left + 1 + i, i == filled - 1 && filled < barWidth ? '>' : '=');
                    if (has_colors()) attroff(COLOR_PAIR(static_cast<int>(ColorToken::TimeBarFilled)));
                } else {
                    if (has_colors()) attron(COLOR_PAIR(static_cast<int>(ColorToken::TimeBarEmpty)));
                    mvaddch(top + 1, left + 1 + i, '-');
                    if (has_colors()) attroff(COLOR_PAIR(static_cast<int>(ColorToken::TimeBarEmpty)));
                }
            }
            
            if (has_colors()) attron(COLOR_PAIR(static_cast<int>(ColorToken::TimeBarEmpty)));
            mvaddch(top + 2, left, '[');
            mvaddch(top + 2, left + barWidth + 1, ']');
            if (has_colors()) attroff(COLOR_PAIR(static_cast<int>(ColorToken::TimeBarEmpty)));
            for (int i = 0; i < barWidth; ++i) {
                if (i < filled) {
                    if (has_colors()) attron(COLOR_PAIR(static_cast<int>(ColorToken::TimeBarFilled)));
                    mvaddch(top + 2, left + 1 + i, '=');
                    if (has_colors()) attroff(COLOR_PAIR(static_cast<int>(ColorToken::TimeBarFilled)));
                } else {
                    if (has_colors()) attron(COLOR_PAIR(static_cast<int>(ColorToken::TimeBarEmpty)));
                    mvaddch(top + 2, left + 1 + i, '-');
                    if (has_colors()) attroff(COLOR_PAIR(static_cast<int>(ColorToken::TimeBarEmpty)));
                }
            }
            if (has_colors()) attron(COLOR_PAIR(static_cast<int>(ColorToken::TimeText)) | A_BOLD);
            else attron(A_BOLD);
            drawTextLine(top + 3, left, width, formatTime(snapshot.positionSeconds) + " / " + formatDuration(snapshot.durationSeconds, snapshot.live));
            if (has_colors()) attroff(COLOR_PAIR(static_cast<int>(ColorToken::TimeText)) | A_BOLD);
            else attroff(A_BOLD);
        } else if (snapshot.live) {
            drawTextLine(top + 1, left, width, "[ LIVE (seek unavailable) ]");
            drawTextLine(top + 2, left, width, "");
            if (height >= 4) {
                attron(A_BOLD);
                drawTextLine(top + 3, left, width, formatTime(snapshot.positionSeconds) + " / " + formatDuration(snapshot.durationSeconds, snapshot.live));
                attroff(A_BOLD);
            }
        } else {
            drawTextLine(top + 1, left, width, "[ no track ]");
            drawTextLine(top + 2, left, width, "");
            if (height >= 4) {
                attron(A_BOLD);
                drawTextLine(top + 3, left, width, formatTime(snapshot.positionSeconds) + " / " + formatDuration(snapshot.durationSeconds, snapshot.live));
                attroff(A_BOLD);
            }
        }
        return;
    }

    if (height >= 2) {
        attron(A_DIM);
        drawTextLine(top, left, width, "Time");
        attroff(A_DIM);

        const int barWidth = std::max(4, width - 2);
        if (snapshot.hasTrack && snapshot.durationSeconds > 0.0 && !snapshot.live) {
            const double fraction = std::clamp(snapshot.positionSeconds / snapshot.durationSeconds, 0.0, 1.0);
            const int filled = static_cast<int>(std::round(fraction * static_cast<double>(barWidth)));
            
            if (has_colors()) attron(COLOR_PAIR(static_cast<int>(ColorToken::TimeBarEmpty)));
            mvaddch(top + 1, left, '[');
            mvaddch(top + 1, left + barWidth + 1, ']');
            if (has_colors()) attroff(COLOR_PAIR(static_cast<int>(ColorToken::TimeBarEmpty)));
            
            for (int i = 0; i < barWidth; ++i) {
                if (i < filled) {
                    if (has_colors()) attron(COLOR_PAIR(static_cast<int>(ColorToken::TimeBarFilled)));
                    mvaddch(top + 1, left + 1 + i, i == filled - 1 && filled < barWidth ? '>' : '=');
                    if (has_colors()) attroff(COLOR_PAIR(static_cast<int>(ColorToken::TimeBarFilled)));
                } else {
                    if (has_colors()) attron(COLOR_PAIR(static_cast<int>(ColorToken::TimeBarEmpty)));
                    mvaddch(top + 1, left + 1 + i, '-');
                    if (has_colors()) attroff(COLOR_PAIR(static_cast<int>(ColorToken::TimeBarEmpty)));
                }
            }
        } else if (snapshot.live) {
            drawTextLine(top + 1, left, width, "[ LIVE (seek unavailable) ]");
        } else {
            drawTextLine(top + 1, left, width, "[ no track ]");
        }

        if (height >= 3) {
            if (has_colors()) attron(COLOR_PAIR(static_cast<int>(ColorToken::TimeText)) | A_BOLD);
            else attron(A_BOLD);
            drawTextLine(top + 2, left, width, formatTime(snapshot.positionSeconds) + " / " + formatDuration(snapshot.durationSeconds, snapshot.live));
            if (has_colors()) attroff(COLOR_PAIR(static_cast<int>(ColorToken::TimeText)) | A_BOLD);
            else attroff(A_BOLD);
        }
        return;
    }

    if (has_colors()) attron(COLOR_PAIR(static_cast<int>(ColorToken::TimeText)) | A_BOLD);
    else attron(A_BOLD);
    drawTextLine(top, left, width, "Time : " + formatTime(snapshot.positionSeconds) + " / " + formatDuration(snapshot.durationSeconds, snapshot.live));
    if (has_colors()) attroff(COLOR_PAIR(static_cast<int>(ColorToken::TimeText)) | A_BOLD);
    else attroff(A_BOLD);
}

void TerminalUI::handleInput(int key) {
    if (key == KEY_MOUSE) {
        MEVENT event;
        if (getmouse(&event) != OK) {
            return;
        }
        if (event.bstate & BUTTON4_PRESSED) {
            if (selectedIndex_ > 0) {
                selectedIndex_ = selectedIndex_ > 2 ? selectedIndex_ - 2 : 0;
            }
        } else if (event.bstate & BUTTON5_PRESSED) {
            if (selectedIndex_ + 1 < engine_.playlist().size()) {
                selectedIndex_ = std::min(selectedIndex_ + 2, engine_.playlist().size() - 1);
            }
        } else if (event.bstate & BUTTON1_CLICKED) {
            if (event.y >= layout_.playlistFrame.top + 1 &&
                event.y < layout_.playlistFrame.top + layout_.playlistFrame.height - 1 &&
                event.x >= layout_.playlistFrame.left + 1 &&
                event.x < layout_.playlistFrame.left + layout_.playlistFrame.width - 1) {
                const int trackCount = static_cast<int>(engine_.playlist().size());
                const int contentRows = layout_.playlistFrame.height - 2;
                const int clickedRow = event.y - (layout_.playlistFrame.top + 1);
                int firstVisible = 0;
                if (static_cast<int>(selectedIndex_) >= contentRows) {
                    firstVisible = static_cast<int>(selectedIndex_) - contentRows + 1;
                }
                const int trackIndex = std::min(firstVisible + clickedRow, trackCount - 1);
                if (trackIndex >= 0 && trackIndex < trackCount) {
                    selectedIndex_ = static_cast<std::size_t>(trackIndex);
                    engine_.playIndex(selectedIndex_);
                }
            }
            if (event.y >= layout_.time.top &&
                event.y < layout_.time.top + layout_.time.height &&
                event.x >= layout_.time.left &&
                event.x < layout_.time.left + layout_.time.width) {
                const auto snap = engine_.snapshot();
                if (snap.hasTrack && snap.durationSeconds > 0.0 && !snap.live && !snap.remote) {
                    const double fraction = static_cast<double>(event.x - layout_.time.left) /
                                            static_cast<double>(layout_.time.width);
                    engine_.seek(fraction * snap.durationSeconds);
                }
            }
            if (event.y >= layout_.cover.top &&
                event.y < layout_.cover.top + layout_.cover.height &&
                event.x >= layout_.cover.left &&
                event.x < layout_.cover.left + layout_.cover.width) {
                cycleCoverArtMode(1);
            }
            if (event.y >= layout_.detailFrame.top &&
                event.y < layout_.detailFrame.top + layout_.detailFrame.height &&
                event.x >= layout_.detailFrame.left &&
                event.x < layout_.detailFrame.left + layout_.detailFrame.width) {
                detailMode_ = detailMode_ == DetailMode::Visualizer ? DetailMode::Lyrics : DetailMode::Visualizer;
            }
        }
        return;
    }

    auto action = keyBinder_.resolve(key);

    if (!activeError_.empty() || settingsOpen_ || !modalBody_.empty()) {
        if (settingsOpen_) {
            switch (action) {
                case Action::Quit:
                    running_ = false;
                    return;
                case Action::DismissModal:
                case Action::TogglePause:
                case Action::OpenWarranty:
                case Action::OpenConditions:
                    closeTransientOverlay();
                    return;
                case Action::SeekBackwardShort:
                case Action::VolumeDown:
                    if (settingsSelectedIndex_ == 0) cycleCoverArtMode(-1);
                    else if (settingsSelectedIndex_ == 1) cycleTheme(-1);
                    else if (settingsSelectedIndex_ == 2) cycleLayoutPreset(-1);
                    return;
                case Action::SeekForwardShort:
                case Action::VolumeUp:
                    if (settingsSelectedIndex_ == 0) cycleCoverArtMode(1);
                    else if (settingsSelectedIndex_ == 1) cycleTheme(1);
                    else if (settingsSelectedIndex_ == 2) cycleLayoutPreset(1);
                    return;
                case Action::PrevTrack:
                    settingsSelectedIndex_ = (settingsSelectedIndex_ + 2) % 3;
                    needsFullRedraw_ = true;
                    return;
                case Action::NextTrack:
                    settingsSelectedIndex_ = (settingsSelectedIndex_ + 1) % 3;
                    needsFullRedraw_ = true;
                    return;
                case Action::CycleRepeatMode:
                    cycleRepeatMode();
                    return;
                case Action::CycleShuffleMode:
                    cycleShuffleMode();
                    return;
                case Action::PlaySelected:
                case Action::OpenSettings:
                    closeTransientOverlay();
                    return;
                default:
                    return;
            }
        }

        switch (action) {
            case Action::Quit:
                running_ = false;
                return;
            case Action::DismissModal:
            case Action::PlaySelected:
            case Action::TogglePause:
                closeTransientOverlay();
                return;
            default:
                return;
        }
    }

    switch (action) {
        case Action::Quit:
            running_ = false;
            return;
        case Action::PrevTrack:
            if (selectedIndex_ > 0) {
                --selectedIndex_;
            }
            return;
        case Action::NextTrack:
            if (selectedIndex_ + 1 < engine_.playlist().size()) {
                ++selectedIndex_;
            }
            return;
        case Action::TogglePause:
            engine_.togglePause();
            return;
        case Action::SeekBackwardLong: {
            const auto snap = engine_.snapshot();
            engine_.seek(snap.positionSeconds - 10.0);
            return;
        }
        case Action::SeekForwardLong: {
            const auto snap = engine_.snapshot();
            engine_.seek(snap.positionSeconds + 10.0);
            return;
        }
        case Action::SeekBackwardShort:
            engine_.seek(engine_.snapshot().positionSeconds - 5.0);
            return;
        case Action::SeekForwardShort:
            engine_.seek(engine_.snapshot().positionSeconds + 5.0);
            return;
        case Action::VolumeUp:
            engine_.adjustVolume(0.05F);
            settings_.volume = engine_.snapshot().volume;
            persistSettings();
            return;
        case Action::VolumeDown:
            engine_.adjustVolume(-0.05F);
            settings_.volume = engine_.snapshot().volume;
            persistSettings();
            return;
        case Action::ToggleDetailMode:
            detailMode_ = detailMode_ == DetailMode::Visualizer ? DetailMode::Lyrics : DetailMode::Visualizer;
            return;
        case Action::CycleCoverMode:
            cycleCoverArtMode(1);
            return;
        case Action::CycleRepeatMode:
            cycleRepeatMode();
            return;
        case Action::CycleShuffleMode:
            cycleShuffleMode();
            return;
        case Action::CycleLayoutPreset:
            cycleLayoutPreset(1);
            return;
        case Action::OpenSettings:
            openSettingsOverlay();
            return;
        case Action::OpenWarranty:
            openWarrantyOverlay();
            return;
        case Action::OpenConditions:
            openConditionsOverlay();
            return;
        case Action::PlaySelected:
            engine_.playIndex(selectedIndex_);
            return;
        case Action::None:
        default:
            return;
    }
}

void TerminalUI::syncErrorOverlay(const PlaybackSnapshot& snapshot) {
    if (snapshot.lastError.empty()) {
        activeError_.clear();
        dismissedError_.clear();
        return;
    }

    if (snapshot.lastError == activeError_ || snapshot.lastError == dismissedError_) {
        return;
    }

    activeError_ = snapshot.lastError;
}

void TerminalUI::openWarrantyOverlay() {
    settingsOpen_ = false;
    modalTitle_ = "Warranty";
    modalSubtitle_ = "RetroWave legal notice";
    modalBody_ =
        "RetroWave Copyright (C) 2026 Viktor Voloshko. "
        "This program comes with ABSOLUTELY NO WARRANTY. "
        "RetroWave is distributed under GPLv3. See LICENSE.txt for the full disclaimer and legal terms.";
}

void TerminalUI::openConditionsOverlay() {
    settingsOpen_ = false;
    modalTitle_ = "Conditions";
    modalSubtitle_ = "RetroWave legal notice";
    modalBody_ =
        "RetroWave Copyright (C) 2026 Viktor Voloshko. "
        "This is free software, and you are welcome to redistribute it under certain conditions. "
        "RetroWave is licensed under GPLv3. See LICENSE.txt for the complete redistribution and modification terms.";
}

void TerminalUI::openSettingsOverlay() {
    modalTitle_.clear();
    modalSubtitle_.clear();
    modalBody_.clear();
    settingsOpen_ = true;
}

void TerminalUI::closeTransientOverlay() {
    if (!activeError_.empty()) {
        dismissedError_ = activeError_;
    }
    activeError_.clear();
    modalTitle_.clear();
    modalSubtitle_.clear();
    modalBody_.clear();
    settingsOpen_ = false;
    needsFullRedraw_ = true;
}

void TerminalUI::cycleCoverArtMode(int delta) {
    const int modeCount = 3;
    int next = static_cast<int>(settings_.coverArtMode) + delta;
    if (next < 0) {
        next = modeCount - 1;
    } else if (next >= modeCount) {
        next = 0;
    }

    settings_.coverArtMode = static_cast<CoverArtMode>(next);
    persistSettings();
}

void TerminalUI::cycleTheme(int delta) {
    if (delta > 0) {
        themeManager_.nextTheme();
    } else {
        themeManager_.prevTheme();
    }
    settings_.theme = themeManager_.current().name;
    themeManager_.applyToNcurses();
    persistSettings();
    needsFullRedraw_ = true;
}

void TerminalUI::cycleRepeatMode() {
    switch (settings_.repeatMode) {
        case RepeatMode::Off:
            settings_.repeatMode = RepeatMode::One;
            break;
        case RepeatMode::One:
            settings_.repeatMode = RepeatMode::All;
            break;
        case RepeatMode::All:
            settings_.repeatMode = RepeatMode::Off;
            break;
    }

    engine_.setRepeatMode(settings_.repeatMode);
    persistSettings();
}

void TerminalUI::cycleShuffleMode() {
    settings_.shuffleMode = settings_.shuffleMode == ShuffleMode::Off ? ShuffleMode::On : ShuffleMode::Off;
    engine_.setShuffleMode(settings_.shuffleMode);
    persistSettings();
}

void TerminalUI::cycleLayoutPreset(int delta) {
    int current = static_cast<int>(layoutPreset_);
    current = (current + delta + 4) % 4;
    layoutPreset_ = static_cast<LayoutPreset>(current);
    
    switch (layoutPreset_) {
        case LayoutPreset::Default: settings_.layoutPreset = "default"; break;
        case LayoutPreset::VisualizerFocus: 
            settings_.layoutPreset = "visualizer"; 
            detailMode_ = DetailMode::Visualizer;
            break;
        case LayoutPreset::LyricsFocus: 
            settings_.layoutPreset = "lyrics"; 
            detailMode_ = DetailMode::Lyrics;
            break;
        case LayoutPreset::Compact: settings_.layoutPreset = "compact"; break;
    }
    
    needsFullRedraw_ = true;
    layoutValid_ = false;
    persistSettings();
}

bool TerminalUI::shutdownRequested() const noexcept {
    return shutdownSignal_ != nullptr && *shutdownSignal_ != 0;
}

void TerminalUI::syncMediaSession(const PlaybackSnapshot& snapshot) {
    if (!snapshot.hasTrack) {
        return;
    }

    const int positionSecond = static_cast<int>(std::round(snapshot.positionSeconds));
    const bool trackChanged = snapshot.path != lastMediaPath_;
    const bool stateChanged = snapshot.state != lastMediaState_;
    const bool positionChanged = positionSecond != lastMediaPositionSecond_;
    if (!trackChanged && !stateChanged && !positionChanged) {
        return;
    }

    const bool isPlaying =
        snapshot.state == PlaybackState::Playing || snapshot.state == PlaybackState::Buffering;
    mediaSession_.updateNowPlaying(
        snapshot.title,
        snapshot.artist,
        snapshot.album,
        snapshot.durationSeconds,
        snapshot.positionSeconds);
    mediaSession_.updatePlaybackState(isPlaying);

    lastMediaPath_ = snapshot.path;
    lastMediaState_ = snapshot.state;
    lastMediaPositionSecond_ = positionSecond;
}

void TerminalUI::flushSettingsFromEngine() {
    settings_.volume = engine_.snapshot().volume;
    settings_.repeatMode = engine_.repeatMode();
    settings_.shuffleMode = engine_.shuffleMode();
    persistSettings();
}

void TerminalUI::persistSettings() {
    settingsStore_.save(settings_);
}

void TerminalUI::setTheme(const std::string& name) {
    if (themeManager_.selectTheme(name)) {
        settings_.theme = themeManager_.current().name;
        themeManager_.applyToNcurses();
        persistSettings();
        needsFullRedraw_ = true;
    }
}

void TerminalUI::setLayoutPreset(const std::string& preset) {
    if (preset == "default") layoutPreset_ = LayoutPreset::Default;
    else if (preset == "visualizer") layoutPreset_ = LayoutPreset::VisualizerFocus;
    else if (preset == "lyrics") layoutPreset_ = LayoutPreset::LyricsFocus;
    else if (preset == "compact") layoutPreset_ = LayoutPreset::Compact;
    else return;
    
    settings_.layoutPreset = preset;
    persistSettings();
    layoutValid_ = false;
    needsFullRedraw_ = true;
}
}  // namespace retrowave
