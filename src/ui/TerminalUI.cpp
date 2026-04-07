#include "ui/TerminalUI.h"

#include <ncurses.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace retrowave {
namespace {

std::size_t nextGlyphBytes(const std::string& value, std::size_t offset, int& columns) {
    if (offset >= value.size()) {
        columns = 0;
        return 0;
    }

    mbstate_t state{};
    wchar_t wide = 0;
    const char* current = value.data() + static_cast<std::ptrdiff_t>(offset);
    const std::size_t remaining = value.size() - offset;
    const std::size_t length = std::mbrtowc(&wide, current, remaining, &state);

    if (length == static_cast<std::size_t>(-1) || length == static_cast<std::size_t>(-2)) {
        columns = 1;
        return 1;
    }

    if (length == 0) {
        columns = 0;
        return 1;
    }

    const int width = wcwidth(wide);
    columns = width < 0 ? 1 : width;
    return length;
}

int displayWidth(const std::string& value) {
    int total = 0;
    for (std::size_t offset = 0; offset < value.size();) {
        int columns = 0;
        const auto length = nextGlyphBytes(value, offset, columns);
        if (length == 0) {
            break;
        }
        total += columns;
        offset += length;
    }
    return total;
}

std::string takeColumns(const std::string& value, int width) {
    if (width <= 0) {
        return {};
    }

    std::size_t endOffset = 0;
    int consumed = 0;
    for (std::size_t offset = 0; offset < value.size();) {
        int columns = 0;
        const auto length = nextGlyphBytes(value, offset, columns);
        if (length == 0) {
            break;
        }
        if (consumed + columns > width) {
            break;
        }
        consumed += columns;
        offset += length;
        endOffset = offset;
    }

    return value.substr(0, endOffset);
}

std::string trimText(const std::string& value, int width) {
    if (width <= 0) {
        return {};
    }

    if (displayWidth(value) <= width) {
        return value;
    }

    if (width <= 3) {
        return takeColumns(value, width);
    }

    return takeColumns(value, width - 3) + "...";
}

std::string formatTime(double seconds) {
    const int total = std::max(0, static_cast<int>(std::round(seconds)));
    const int minutes = total / 60;
    const int remainder = total % 60;

    std::ostringstream stream;
    stream << minutes << ':';
    if (remainder < 10) {
        stream << '0';
    }
    stream << remainder;
    return stream.str();
}

std::vector<std::string> wrapText(const std::string& text, int width) {
    std::vector<std::string> lines;
    if (width <= 0) {
        return lines;
    }

    std::string remaining = text;
    while (!remaining.empty()) {
        const auto line = takeColumns(remaining, width);
        if (line.empty()) {
            lines.push_back(remaining.substr(0, 1));
            remaining.erase(0, 1);
            continue;
        }
        lines.push_back(line);
        remaining.erase(0, line.size());
    }

    if (lines.empty()) {
        lines.push_back({});
    }

    return lines;
}

void drawTextLine(int row, int column, int width, const std::string& text) {
    if (width <= 0) {
        return;
    }

    mvhline(row, column, ' ', width);
    const auto clipped = trimText(text, width);
    if (!clipped.empty()) {
        mvaddnstr(row, column, clipped.c_str(), static_cast<int>(clipped.size()));
    }
}

float sampleVisualizer(const std::vector<float>& bins, double position) {
    if (bins.empty()) {
        return 0.0F;
    }

    position = std::clamp(position, 0.0, 1.0);
    const double scaled = position * static_cast<double>(bins.size() - 1);
    const auto leftIndex = static_cast<std::size_t>(scaled);
    const auto rightIndex = std::min(leftIndex + 1, bins.size() - 1);
    const float fraction = static_cast<float>(scaled - static_cast<double>(leftIndex));
    return bins[leftIndex] * (1.0F - fraction) + bins[rightIndex] * fraction;
}

std::vector<std::string> renderPlaceholderArt(int width, int height) {
    std::vector<std::string> lines(
        static_cast<std::size_t>(std::max(1, height)),
        std::string(static_cast<std::size_t>(std::max(1, width)), ' '));
    if (width < 8 || height < 4) {
        return lines;
    }

    const int mid = height / 2;
    const std::string title = "NO COVER";
    const int titleOffset = std::max(0, (width - static_cast<int>(title.size())) / 2);
    lines[static_cast<std::size_t>(mid)] = std::string(static_cast<std::size_t>(width), ' ');
    lines[static_cast<std::size_t>(mid)].replace(static_cast<std::size_t>(titleOffset), title.size(), title);

    for (int row = 0; row < height; ++row) {
        lines[static_cast<std::size_t>(row)][0] = row == 0 || row == height - 1 ? '+' : '|';
        lines[static_cast<std::size_t>(row)][static_cast<std::size_t>(width - 1)] = row == 0 || row == height - 1 ? '+' : '|';
    }
    for (int column = 1; column < width - 1; ++column) {
        lines.front()[static_cast<std::size_t>(column)] = '-';
        lines.back()[static_cast<std::size_t>(column)] = '-';
    }

    return lines;
}

std::vector<std::string> renderAsciiArt(const std::shared_ptr<const AlbumArt>& art, int maxWidth, int maxHeight) {
    if (!art || art->empty() || maxWidth <= 0 || maxHeight <= 0) {
        return renderPlaceholderArt(maxWidth, maxHeight);
    }

    static const std::string kRamp = " .:-=+*#%@";
    constexpr double kCharAspect = 0.5;

    const double imageAspect = static_cast<double>(art->width) / static_cast<double>(std::max(1, art->height));
    int targetWidth = std::max(1, std::min(maxWidth, static_cast<int>(std::round(maxHeight * imageAspect / kCharAspect))));
    int targetHeight = std::max(1, std::min(maxHeight, static_cast<int>(std::round(targetWidth * kCharAspect / imageAspect))));

    if (targetHeight > maxHeight) {
        targetHeight = maxHeight;
        targetWidth = std::max(1, std::min(maxWidth, static_cast<int>(std::round(targetHeight * imageAspect / kCharAspect))));
    }

    std::vector<float> sampled(static_cast<std::size_t>(targetWidth * targetHeight), 0.0F);
    float minGray = 255.0F;
    float maxGray = 0.0F;

    for (int row = 0; row < targetHeight; ++row) {
        const int y0 = row * art->height / targetHeight;
        const int y1 = std::max(y0 + 1, (row + 1) * art->height / targetHeight);

        for (int column = 0; column < targetWidth; ++column) {
            const int x0 = column * art->width / targetWidth;
            const int x1 = std::max(x0 + 1, (column + 1) * art->width / targetWidth);

            std::uint64_t sum = 0;
            std::size_t count = 0;
            for (int sourceY = y0; sourceY < y1; ++sourceY) {
                for (int sourceX = x0; sourceX < x1; ++sourceX) {
                    sum += art->grayscale[static_cast<std::size_t>(sourceY * art->width + sourceX)];
                    ++count;
                }
            }

            const float average = count > 0 ? static_cast<float>(sum) / static_cast<float>(count) : 0.0F;
            sampled[static_cast<std::size_t>(row * targetWidth + column)] = average;
            minGray = std::min(minGray, average);
            maxGray = std::max(maxGray, average);
        }
    }

    const float span = std::max(1.0F, maxGray - minGray);
    std::vector<std::string> lines;
    lines.reserve(static_cast<std::size_t>(targetHeight));

    for (int row = 0; row < targetHeight; ++row) {
        std::string line;
        line.reserve(static_cast<std::size_t>(targetWidth));

        for (int column = 0; column < targetWidth; ++column) {
            const float gray = sampled[static_cast<std::size_t>(row * targetWidth + column)];
            const float normalized = std::clamp((gray - minGray) / span, 0.0F, 1.0F);
            const std::size_t rampIndex = static_cast<std::size_t>(
                std::round((1.0F - normalized) * static_cast<float>(kRamp.size() - 1)));
            line.push_back(kRamp[rampIndex]);
        }

        lines.push_back(std::move(line));
    }

    return lines;
}

int currentLyricIndex(const LyricsData& lyrics, double positionSeconds) {
    if (!lyrics.timed) {
        return -1;
    }

    int current = -1;
    for (std::size_t index = 0; index < lyrics.lines.size(); ++index) {
        if (lyrics.lines[index].timestampSeconds <= positionSeconds) {
            current = static_cast<int>(index);
        } else {
            break;
        }
    }
    return current;
}

std::string lyricPrefix(const LyricLine& line, bool timed) {
    if (!timed || line.timestampSeconds < 0.0) {
        return {};
    }

    const int total = static_cast<int>(std::round(line.timestampSeconds));
    const int minutes = total / 60;
    const int seconds = total % 60;

    std::ostringstream stream;
    stream << '[' << std::setw(2) << std::setfill('0') << minutes << ':' << std::setw(2) << seconds << "] ";
    return stream.str();
}

}  // namespace

TerminalUI::TerminalUI(PlaybackEngine& engine) : engine_(engine) {}

int TerminalUI::run() {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_CYAN, -1);
        init_pair(2, COLOR_GREEN, -1);
        init_pair(3, COLOR_YELLOW, -1);
        init_pair(4, COLOR_MAGENTA, -1);
        init_pair(5, COLOR_BLACK, COLOR_YELLOW);
        init_pair(6, COLOR_BLACK, COLOR_CYAN);
    }

    while (running_) {
        engine_.update();
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
        draw(snapshot, std::chrono::steady_clock::now());
    }

    endwin();
    return 0;
}

TerminalUI::Layout TerminalUI::computeLayout(int rows, int cols) const {
    Layout layout;
    layout.rows = rows;
    layout.cols = cols;
    layout.header = {0, 0, 1, cols};

    const int playlistWidth = std::max(32, cols / 3);
    const int rightWidth = cols - playlistWidth - 3;
    const int contentHeight = rows - 2;
    const int detailHeight = std::max(9, contentHeight / 2);
    const int cardHeight = contentHeight - detailHeight;

    layout.playlistFrame = {1, 1, contentHeight, playlistWidth};
    layout.playlistContent = {2, 2, contentHeight - 2, playlistWidth - 2};
    layout.albumFrame = {1, playlistWidth + 2, cardHeight, rightWidth};

    const int albumTop = 2;
    const int albumLeft = playlistWidth + 3;
    const int albumHeight = cardHeight - 2;
    const int albumWidth = rightWidth - 2;
    const int artWidth = std::clamp(albumWidth / 3, 18, 28);
    const int infoLeft = albumLeft + artWidth + 2;
    const int infoWidth = std::max(12, albumWidth - artWidth - 2);
    const int timeHeight = albumHeight >= 6 ? 2 : 1;
    const int gapHeight = albumHeight >= 8 ? 1 : 0;
    const int metaHeight = std::max(1, albumHeight - timeHeight - gapHeight);

    layout.cover = {albumTop, albumLeft, albumHeight, artWidth};
    layout.meta = {albumTop, infoLeft, metaHeight, infoWidth};
    layout.time = {albumTop + metaHeight + gapHeight, infoLeft, timeHeight, infoWidth};

    layout.detailFrame = {1 + cardHeight, playlistWidth + 2, detailHeight, rightWidth};
    layout.detailContent = {2 + cardHeight, playlistWidth + 3, detailHeight - 2, rightWidth - 2};
    return layout;
}

int TerminalUI::computePollTimeout(
    const PlaybackSnapshot& snapshot,
    std::chrono::steady_clock::time_point now) const {
    using namespace std::chrono;

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

void TerminalUI::drawHeader(const Rect& rect) const {
    mvhline(rect.top, rect.left, ' ', rect.width);
    attron(COLOR_PAIR(1) | A_BOLD);
    mvprintw(rect.top, rect.left + 2, "RetroWave");
    attroff(COLOR_PAIR(1) | A_BOLD);
    mvprintw(rect.top, rect.left + 14, "Terminal player with ASCII album art and .lrc lyrics");
}

void TerminalUI::draw(const PlaybackSnapshot& snapshot, std::chrono::steady_clock::time_point now) {
    int rows = 0;
    int cols = 0;
    getmaxyx(stdscr, rows, cols);

    const int minWidth = 96;
    const int minHeight = 24;
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

    const bool overlayVisible = !activeError_.empty() || !modalBody_.empty();
    if (!overlayVisible && overlayVisibleLastFrame_) {
        needsFullRedraw_ = true;
    }
    overlayVisibleLastFrame_ = overlayVisible;

    if (overlayVisible) {
        erase();
        if (!activeError_.empty()) {
            drawModalOverlay(rows, cols, "Error", "Playback backend or decode failure", activeError_);
        } else {
            drawLegalScreen(rows, cols);
        }
        wnoutrefresh(stdscr);
        doupdate();
        return;
    }

    const Layout nextLayout = computeLayout(rows, cols);
    if (!layoutValid_ ||
        nextLayout.rows != layout_.rows ||
        nextLayout.cols != layout_.cols ||
        nextLayout.playlistFrame.width != layout_.playlistFrame.width ||
        nextLayout.albumFrame.height != layout_.albumFrame.height ||
        nextLayout.detailFrame.height != layout_.detailFrame.height) {
        layout_ = nextLayout;
        layoutValid_ = true;
        needsFullRedraw_ = true;
        visualizerState_.clear();
    } else {
        layout_ = nextLayout;
    }

    const bool fullRedraw = needsFullRedraw_;
    const bool playlistDirty =
        fullRedraw || selectedIndex_ != lastSelectedIndex_ || snapshot.currentIndex != lastCurrentIndex_ ||
        snapshot.hasTrack != lastHasTrack_;
    const bool coverDirty = fullRedraw || snapshot.path != lastTrackPath_;
    const bool metaDirty =
        fullRedraw || snapshot.path != lastTrackPath_ || snapshot.title != lastTitle_ || snapshot.artist != lastArtist_ ||
        snapshot.album != lastAlbum_ || snapshot.paused != lastPaused_ || snapshot.loading != lastLoading_ ||
        (snapshot.lyrics && snapshot.lyrics->found) != lastLyricsFound_ ||
        static_cast<int>(std::round(snapshot.volume * 100.0F)) != lastVolumePercent_;
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
        const int activeLyricIndex =
            snapshot.lyrics ? currentLyricIndex(*snapshot.lyrics, snapshot.positionSeconds) : -1;
        detailDirty = detailDirty || snapshot.path != lastTrackPath_ || activeLyricIndex != lastActiveLyricIndex_;
        lastActiveLyricIndex_ = activeLyricIndex;
    }

    if (fullRedraw) {
        erase();
        drawHeader(layout_.header);
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
            drawLyrics(
                layout_.detailContent.top,
                layout_.detailContent.left,
                layout_.detailContent.height,
                layout_.detailContent.width,
                snapshot);
        } else {
            drawVisualizer(
                layout_.detailContent.top,
                layout_.detailContent.left,
                layout_.detailContent.height,
                layout_.detailContent.width,
                snapshot);
        }
    }

    if (!activeError_.empty()) {
        drawModalOverlay(rows, cols, "Error", "Playback backend or decode failure", activeError_);
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
    lastPaused_ = snapshot.paused;
    lastLoading_ = snapshot.loading;
    lastLyricsFound_ = snapshot.lyrics && snapshot.lyrics->found;
    lastVolumePercent_ = static_cast<int>(std::round(snapshot.volume * 100.0F));
    lastPositionSecond_ = static_cast<int>(std::round(snapshot.positionSeconds));
    lastDetailMode_ = detailMode_;
}

void TerminalUI::drawFrame(int top, int left, int height, int width, const char* title) const {
    mvaddch(top, left, ACS_ULCORNER);
    mvhline(top, left + 1, ACS_HLINE, width - 2);
    mvaddch(top, left + width - 1, ACS_URCORNER);
    mvvline(top + 1, left, ACS_VLINE, height - 2);
    mvvline(top + 1, left + width - 1, ACS_VLINE, height - 2);
    mvaddch(top + height - 1, left, ACS_LLCORNER);
    mvhline(top + height - 1, left + 1, ACS_HLINE, width - 2);
    mvaddch(top + height - 1, left + width - 1, ACS_LRCORNER);

    attron(A_BOLD);
    mvprintw(top, left + 2, " %s ", title);
    attroff(A_BOLD);
}

void TerminalUI::drawPlaylist(int top, int left, int height, int width, const PlaybackSnapshot& snapshot) const {
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

        if (isSelected) {
            attron(A_REVERSE);
        }
        if (isCurrent && has_colors()) {
            attron(COLOR_PAIR(2) | A_BOLD);
        }

        drawTextLine(top + row, left, width, line);

        if (isCurrent && has_colors()) {
            attroff(COLOR_PAIR(2) | A_BOLD);
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
    const auto artLines = renderAsciiArt(snapshot.albumArt, width, height);
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

    const std::string state = snapshot.loading ? "loading" : (snapshot.paused ? "paused" : "playing");
    const std::string lyricsStatus = snapshot.lyrics && snapshot.lyrics->found ? "loaded" : "missing";

    std::vector<std::string> lines;
    lines.reserve(7);
    lines.push_back(trimText(snapshot.title, width));
    lines.push_back({});
    lines.push_back("Artist : " + trimText(snapshot.artist.empty() ? "Unknown" : snapshot.artist, std::max(0, width - 9)));
    lines.push_back("Album  : " + trimText(snapshot.album.empty() ? "Unknown" : snapshot.album, std::max(0, width - 9)));
    lines.push_back("State  : " + trimText(state, std::max(0, width - 9)));
    lines.push_back("Volume : " + std::to_string(static_cast<int>(std::round(snapshot.volume * 100.0F))) + "%");
    lines.push_back("Lyrics : " + lyricsStatus);

    for (int row = 0; row < height; ++row) {
        mvhline(top + row, left, ' ', width);
    }

    for (int row = 0; row < std::min(height, static_cast<int>(lines.size())); ++row) {
        if (row == 0) {
            attron(A_BOLD);
        }
        drawTextLine(top + row, left, width, lines[static_cast<std::size_t>(row)]);
        if (row == 0) {
            attroff(A_BOLD);
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

    if (height >= 2) {
        attron(A_DIM);
        drawTextLine(top, left, width, "Time");
        attroff(A_DIM);
        attron(A_BOLD);
        drawTextLine(top + 1, left, width, formatTime(snapshot.positionSeconds) + " / " + formatTime(snapshot.durationSeconds));
        attroff(A_BOLD);
        return;
    }

    attron(A_BOLD);
    drawTextLine(top, left, width, "Time : " + formatTime(snapshot.positionSeconds) + " / " + formatTime(snapshot.durationSeconds));
    attroff(A_BOLD);
}

void TerminalUI::drawVisualizer(int top, int left, int height, int width, const PlaybackSnapshot& snapshot) const {
    if (!snapshot.hasTrack || snapshot.visualizer.empty() || height < 4 || width < 8) {
        mvprintw(top, left, "Visualizer will appear when a track is loaded.");
        return;
    }

    visualizerState_.resize(static_cast<std::size_t>(width), 0.0F);
    ++visualizerTick_;

    const int barHeight = height - 2;
    const double duration = std::max(0.001, snapshot.durationSeconds);
    const double progress = std::clamp(snapshot.positionSeconds / duration, 0.0, 1.0);

    std::vector<float> targets(static_cast<std::size_t>(width), 0.0F);
    float localPeak = 0.0F;

    for (int column = 0; column < width; ++column) {
        const double position = static_cast<double>(column) / std::max(1, width - 1);
        const double animatedOffset = std::fmod(
            progress + position * 0.55 + static_cast<double>(visualizerTick_ % 48) / 96.0,
            1.0);
        const float amplitude = sampleVisualizer(snapshot.visualizer, animatedOffset);
        targets[static_cast<std::size_t>(column)] = amplitude;
        localPeak = std::max(localPeak, amplitude);
    }

    const float autoGain = localPeak > 0.0001F ? 1.0F / localPeak : 1.0F;
    for (int column = 0; column < width; ++column) {
        auto normalized = std::clamp(targets[static_cast<std::size_t>(column)] * autoGain, 0.0F, 1.0F);
        normalized = std::pow(normalized, 0.72F);
        normalized = std::clamp(normalized * (0.65F + snapshot.level * 0.8F), 0.0F, 1.0F);

        auto& state = visualizerState_[static_cast<std::size_t>(column)];
        if (normalized > state) {
            state = normalized;
        } else {
            state = std::max(normalized, state * 0.86F - 0.01F);
        }

        const int filled = std::clamp(
            static_cast<int>(std::round(state * static_cast<float>(barHeight))),
            0,
            barHeight);
        const bool highlight = column % 3 != 1;

        for (int row = 0; row < barHeight; ++row) {
            const int screenRow = top + barHeight - 1 - row;
            if (row < filled) {
                if (has_colors()) {
                    attron((highlight ? COLOR_PAIR(4) : COLOR_PAIR(2)) | A_BOLD);
                }
                mvaddch(screenRow, left + column, row == filled - 1 ? '#' : '|');
                if (has_colors()) {
                    attroff((highlight ? COLOR_PAIR(4) : COLOR_PAIR(2)) | A_BOLD);
                }
            } else if (row == 0) {
                mvaddch(screenRow, left + column, '.');
            }
        }
    }

    mvprintw(top + height - 1, left, "Auto gain x%.1f   level %3d%%", autoGain, static_cast<int>(std::round(snapshot.level * 100.0F)));
}

void TerminalUI::drawLyrics(int top, int left, int height, int width, const PlaybackSnapshot& snapshot) const {
    if (!snapshot.lyrics || !snapshot.lyrics->found) {
        const std::string message = snapshot.lyrics ? snapshot.lyrics->message : "Could not find .lrc lyrics for this track.";
        drawTextLine(top + height / 2, left, width, message);
        return;
    }

    if (snapshot.lyrics->lines.empty()) {
        drawTextLine(top + height / 2, left, width, snapshot.lyrics->message);
        return;
    }

    const int bodyRows = std::max(1, height - 1);
    const int activeIndex = currentLyricIndex(*snapshot.lyrics, snapshot.positionSeconds);

    int firstVisible = 0;
    if (snapshot.lyrics->timed && activeIndex >= 0) {
        firstVisible = std::max(0, activeIndex - std::max(1, bodyRows / 2));
        firstVisible = std::min(
            firstVisible,
            std::max(0, static_cast<int>(snapshot.lyrics->lines.size()) - bodyRows));
    }

    for (int row = 0; row < bodyRows; ++row) {
        const int lineIndex = firstVisible + row;
        if (lineIndex >= static_cast<int>(snapshot.lyrics->lines.size())) {
            break;
        }

        const auto& lyric = snapshot.lyrics->lines[static_cast<std::size_t>(lineIndex)];
        const std::string prefix = lyricPrefix(lyric, snapshot.lyrics->timed);
        const std::string rendered = trimText(prefix + lyric.text, width);
        const bool isActive = lineIndex == activeIndex;
        const bool isPast = snapshot.lyrics->timed && activeIndex >= 0 && lineIndex < activeIndex;

        if (isActive) {
            if (has_colors()) {
                attron(COLOR_PAIR(3) | A_BOLD);
            } else {
                attron(A_BOLD);
            }
        } else if (isPast) {
            attron(A_DIM);
        }

        drawTextLine(top + row, left, width, rendered);

        if (isActive) {
            if (has_colors()) {
                attroff(COLOR_PAIR(3) | A_BOLD);
            } else {
                attroff(A_BOLD);
            }
        } else if (isPast) {
            attroff(A_DIM);
        }
    }

    drawTextLine(top + height - 1, left, width, snapshot.lyrics->message);
}

void TerminalUI::drawModalOverlay(
    int rows,
    int cols,
    const std::string& title,
    const std::string& subtitle,
    const std::string& body) const {
    const int overlayWidth = std::min(cols - 8, std::max(52, cols * 2 / 3));
    const auto wrappedBody = wrapText(body, overlayWidth - 6);
    const int messageLines = std::min<int>(wrappedBody.size(), std::max(3, rows - 14));
    const int overlayHeight = std::min(rows - 6, 6 + messageLines);
    const int top = std::max(2, (rows - overlayHeight) / 2);
    const int left = std::max(2, (cols - overlayWidth) / 2);

    if (has_colors()) {
        attron(COLOR_PAIR(3) | A_DIM);
    } else {
        attron(A_DIM);
    }
    for (int row = 1; row < rows - 1; ++row) {
        mvhline(row, 1, ' ', cols - 2);
    }
    if (has_colors()) {
        attroff(COLOR_PAIR(3) | A_DIM);
    } else {
        attroff(A_DIM);
    }

    WINDOW* overlay = newwin(overlayHeight, overlayWidth, top, left);
    if (overlay == nullptr) {
        return;
    }

    if (has_colors()) {
        wbkgd(overlay, COLOR_PAIR(5) | ' ');
    } else {
        wbkgd(overlay, A_REVERSE);
    }
    werase(overlay);
    box(overlay, 0, 0);

    wattron(overlay, A_BOLD);
    mvwprintw(overlay, 0, 2, " %s ", title.c_str());
    mvwaddnstr(overlay, 1, 2, trimText(subtitle, overlayWidth - 4).c_str(), -1);
    wattroff(overlay, A_BOLD);

    for (int index = 0; index < messageLines; ++index) {
        mvwhline(overlay, 2 + index, 2, ' ', overlayWidth - 4);
        mvwaddnstr(
            overlay,
            2 + index,
            2,
            trimText(wrappedBody[static_cast<std::size_t>(index)], overlayWidth - 4).c_str(),
            -1);
    }

    if (static_cast<int>(wrappedBody.size()) > messageLines) {
        mvwprintw(overlay, 2 + messageLines - 1, overlayWidth - 6, "%s", "...");
    }

    if (has_colors()) {
        wattron(overlay, COLOR_PAIR(6) | A_BOLD);
    } else {
        wattron(overlay, A_BOLD);
    }
    mvwhline(overlay, overlayHeight - 2, 2, ' ', overlayWidth - 4);
    mvwaddnstr(overlay, overlayHeight - 2, 2, "Enter/Esc dismiss   q quit", -1);
    if (has_colors()) {
        wattroff(overlay, COLOR_PAIR(6) | A_BOLD);
    } else {
        wattroff(overlay, A_BOLD);
    }

    wrefresh(overlay);
    delwin(overlay);
}

void TerminalUI::drawLegalScreen(int rows, int cols) const {
    erase();

    const int inset = 2;
    const int boxTop = inset;
    const int boxLeft = inset;
    const int boxHeight = std::max(8, rows - inset * 2);
    const int boxWidth = std::max(20, cols - inset * 2);
    const int bodyWidth = std::max(20, boxWidth - 6);
    const auto wrappedBody = wrapText(modalBody_, bodyWidth);
    const int maxBodyLines = std::max(1, boxHeight - 8);

    if (has_colors()) {
        attron(COLOR_PAIR(1) | A_BOLD);
    } else {
        attron(A_BOLD);
    }
    mvprintw(0, 2, "RetroWave");
    if (has_colors()) {
        attroff(COLOR_PAIR(1) | A_BOLD);
    } else {
        attroff(A_BOLD);
    }
    mvprintw(0, 14, "Legal Notice");

    drawFrame(boxTop, boxLeft, boxHeight, boxWidth, modalTitle_.c_str());

    if (has_colors()) {
        attron(COLOR_PAIR(3) | A_BOLD);
    } else {
        attron(A_BOLD);
    }
    drawTextLine(boxTop + 2, boxLeft + 3, bodyWidth, modalSubtitle_);
    if (has_colors()) {
        attroff(COLOR_PAIR(3) | A_BOLD);
    } else {
        attroff(A_BOLD);
    }

    for (int index = 0; index < std::min<int>(wrappedBody.size(), maxBodyLines); ++index) {
        drawTextLine(boxTop + 4 + index, boxLeft + 3, bodyWidth, wrappedBody[static_cast<std::size_t>(index)]);
    }

    if (static_cast<int>(wrappedBody.size()) > maxBodyLines) {
        mvprintw(boxTop + 4 + maxBodyLines - 1, boxLeft + boxWidth - 6, "%s", "...");
    }

    if (has_colors()) {
        attron(COLOR_PAIR(6) | A_BOLD);
    } else {
        attron(A_BOLD);
    }
    mvprintw(boxTop + boxHeight - 3, boxLeft + 3, "%s", "Enter/Esc dismiss   q quit");
    if (has_colors()) {
        attroff(COLOR_PAIR(6) | A_BOLD);
    } else {
        attroff(A_BOLD);
    }
}

void TerminalUI::handleInput(int key) {
    if (!activeError_.empty() || !modalBody_.empty()) {
        switch (key) {
            case 'q':
            case 'Q':
                running_ = false;
                return;
            case 27:
            case '\n':
            case KEY_ENTER:
            case ' ':
                if (!activeError_.empty()) {
                    dismissedError_ = activeError_;
                }
                activeError_.clear();
                modalTitle_.clear();
                modalSubtitle_.clear();
                modalBody_.clear();
                return;
            default:
                return;
        }
    }

    switch (key) {
        case 'q':
        case 'Q':
            running_ = false;
            return;
        case KEY_UP:
            if (selectedIndex_ > 0) {
                --selectedIndex_;
            }
            return;
        case KEY_DOWN:
            if (selectedIndex_ + 1 < engine_.playlist().size()) {
                ++selectedIndex_;
            }
            return;
        case '\n':
        case KEY_ENTER:
            engine_.playIndex(selectedIndex_);
            return;
        case ' ':
            engine_.togglePause();
            return;
        case 'n':
        case 'N':
            if (selectedIndex_ + 1 < engine_.playlist().size()) {
                ++selectedIndex_;
            }
            engine_.next();
            return;
        case 'p':
        case 'P':
            if (selectedIndex_ > 0) {
                --selectedIndex_;
            }
            engine_.previous();
            return;
        case '+':
        case '=':
            engine_.adjustVolume(0.05F);
            return;
        case '-':
        case '_':
            engine_.adjustVolume(-0.05F);
            return;
        case 't':
        case 'T':
            detailMode_ = detailMode_ == DetailMode::Visualizer ? DetailMode::Lyrics : DetailMode::Visualizer;
            return;
        case 'w':
        case 'W':
            openWarrantyOverlay();
            return;
        case 'c':
        case 'C':
            openConditionsOverlay();
            return;
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
    modalTitle_ = "Warranty";
    modalSubtitle_ = "RetroWave legal notice";
    modalBody_ =
        "RetroWave Copyright (C) 2026 Viktor Voloshko. "
        "This program comes with ABSOLUTELY NO WARRANTY. "
        "RetroWave is distributed under GPLv3. See LICENSE.txt for the full disclaimer and legal terms.";
}

void TerminalUI::openConditionsOverlay() {
    modalTitle_ = "Conditions";
    modalSubtitle_ = "RetroWave legal notice";
    modalBody_ =
        "RetroWave Copyright (C) 2026 Viktor Voloshko. "
        "This is free software, and you are welcome to redistribute it under certain conditions. "
        "RetroWave is licensed under GPLv3. See LICENSE.txt for the complete redistribution and modification terms.";
}

}  // namespace retrowave
