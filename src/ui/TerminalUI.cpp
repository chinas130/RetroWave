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

const char* coverArtModeLabel(CoverArtMode mode) {
    switch (mode) {
        case CoverArtMode::Ascii:
            return "ASCII";
        case CoverArtMode::BlockShading:
            return "Block Shading";
        case CoverArtMode::HalfBlock:
            return "Half-Block";
    }

    return "ASCII";
}

const char* repeatModeLabel(RepeatMode mode) {
    switch (mode) {
        case RepeatMode::Off:
            return "off";
        case RepeatMode::One:
            return "one";
        case RepeatMode::All:
            return "all";
    }

    return "off";
}

const char* shuffleModeLabel(ShuffleMode mode) {
    switch (mode) {
        case ShuffleMode::Off:
            return "off";
        case ShuffleMode::On:
            return "on";
    }

    return "off";
}

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

std::string formatDuration(double seconds, bool live) {
    if (live || seconds <= 0.0) {
        return "LIVE";
    }
    return formatTime(seconds);
}

std::string playbackStateLabel(PlaybackState state) {
    switch (state) {
        case PlaybackState::Idle:
            return "idle";
        case PlaybackState::Loading:
            return "loading";
        case PlaybackState::Playing:
            return "playing";
        case PlaybackState::Paused:
            return "paused";
        case PlaybackState::Buffering:
            return "buffering";
        case PlaybackState::Ended:
            return "ended";
        case PlaybackState::Failed:
            return "failed";
    }
    return "unknown";
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

std::vector<float> sampleAlbumArt(
    const std::shared_ptr<const AlbumArt>& art,
    int targetWidth,
    int targetHeight,
    float& minGray,
    float& maxGray) {
    std::vector<float> sampled(static_cast<std::size_t>(targetWidth * targetHeight), 0.0F);
    minGray = 255.0F;
    maxGray = 0.0F;

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

    return sampled;
}

std::vector<std::string> renderCoverArt(
    const std::shared_ptr<const AlbumArt>& art,
    int maxWidth,
    int maxHeight,
    CoverArtMode mode) {
    if (!art || art->empty() || maxWidth <= 0 || maxHeight <= 0) {
        return renderPlaceholderArt(maxWidth, maxHeight);
    }

    constexpr double kCharAspect = 0.5;

    const double imageAspect = static_cast<double>(art->width) / static_cast<double>(std::max(1, art->height));
    int targetWidth = std::max(1, std::min(maxWidth, static_cast<int>(std::round(maxHeight * imageAspect / kCharAspect))));
    int targetHeight = std::max(1, std::min(maxHeight, static_cast<int>(std::round(targetWidth * kCharAspect / imageAspect))));

    if (mode == CoverArtMode::HalfBlock) {
        targetWidth = std::max(1, std::min(maxWidth, static_cast<int>(std::round(maxHeight * imageAspect / 1.0))));
        targetHeight = std::max(1, std::min(maxHeight * 2, static_cast<int>(std::round(targetWidth / imageAspect))));
    }

    if (targetHeight > maxHeight) {
        targetHeight = maxHeight;
        targetWidth = std::max(1, std::min(maxWidth, static_cast<int>(std::round(targetHeight * imageAspect / kCharAspect))));
    }

    if (mode == CoverArtMode::HalfBlock && targetHeight > maxHeight * 2) {
        targetHeight = maxHeight * 2;
        targetWidth = std::max(1, std::min(maxWidth, static_cast<int>(std::round((targetHeight / 2.0) * imageAspect))));
    }

    float minGray = 255.0F;
    float maxGray = 0.0F;
    const auto sampled = sampleAlbumArt(art, targetWidth, targetHeight, minGray, maxGray);
    const float span = std::max(1.0F, maxGray - minGray);
    std::vector<std::string> lines;
    if (mode == CoverArtMode::Ascii) {
        static const std::string kRamp = "  ..,,::--==++**##%%@@";
        lines.reserve(static_cast<std::size_t>(targetHeight));
        for (int row = 0; row < targetHeight; ++row) {
            std::string line;
            line.reserve(static_cast<std::size_t>(targetWidth));

            for (int column = 0; column < targetWidth; ++column) {
                const float gray = sampled[static_cast<std::size_t>(row * targetWidth + column)];
                const float normalized = std::pow(std::clamp((gray - minGray) / span, 0.0F, 1.0F), 1.12F);
                const std::size_t rampIndex = static_cast<std::size_t>(
                    std::round((1.0F - normalized) * static_cast<float>(kRamp.size() - 1)));
                line.push_back(kRamp[rampIndex]);
            }

            lines.push_back(std::move(line));
        }
        return lines;
    }

    if (mode == CoverArtMode::BlockShading) {
        static const std::vector<std::string> kRamp = {" ", "░", "▒", "▓", "█"};
        lines.reserve(static_cast<std::size_t>(targetHeight));
        for (int row = 0; row < targetHeight; ++row) {
            std::string line;
            for (int column = 0; column < targetWidth; ++column) {
                const float gray = sampled[static_cast<std::size_t>(row * targetWidth + column)];
                const float normalized = std::pow(std::clamp((gray - minGray) / span, 0.0F, 1.0F), 1.05F);
                const std::size_t rampIndex = static_cast<std::size_t>(
                    std::round((1.0F - normalized) * static_cast<float>(kRamp.size() - 1)));
                line += kRamp[rampIndex];
            }
            lines.push_back(std::move(line));
        }
        return lines;
    }

    lines.reserve(static_cast<std::size_t>((targetHeight + 1) / 2));
    for (int row = 0; row < targetHeight; row += 2) {
        std::string line;
        for (int column = 0; column < targetWidth; ++column) {
            const float topGray = sampled[static_cast<std::size_t>(row * targetWidth + column)];
            const float bottomGray = sampled[static_cast<std::size_t>(std::min(row + 1, targetHeight - 1) * targetWidth + column)];
            const float topDark = 1.0F - std::clamp((topGray - minGray) / span, 0.0F, 1.0F);
            const float bottomDark = 1.0F - std::clamp((bottomGray - minGray) / span, 0.0F, 1.0F);

            if (topDark < 0.18F && bottomDark < 0.18F) {
                line += " ";
            } else if (topDark > 0.72F && bottomDark > 0.72F) {
                line += "█";
            } else if (topDark >= bottomDark) {
                line += topDark > 0.38F ? "▀" : " ";
            } else {
                line += bottomDark > 0.38F ? "▄" : " ";
            }
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

double lineProgress(const LyricLine& line, double positionSeconds) {
    if (line.timestampSeconds < 0.0 || line.endTimestampSeconds < 0.0) {
        return 0.0;
    }
    const double duration = line.endTimestampSeconds - line.timestampSeconds;
    if (duration <= 0.0) {
        return 1.0;
    }
    return std::clamp((positionSeconds - line.timestampSeconds) / duration, 0.0, 1.0);
}

int activeSegmentIndex(const LyricLine& line, double positionSeconds) {
    if (!line.hasWordSync()) {
        return -1;
    }
    int current = -1;
    for (std::size_t i = 0; i < line.segments.size(); ++i) {
        if (line.segments[i].timestampSeconds <= positionSeconds) {
            current = static_cast<int>(i);
        } else {
            break;
        }
    }
    return current;
}

}  // namespace

TerminalUI::TerminalUI(PlaybackEngine& engine, const volatile std::sig_atomic_t* shutdownSignal)
    : engine_(engine), shutdownSignal_(shutdownSignal) {
    settings_ = settingsStore_.load();
    engine_.setVolume(settings_.volume);
    engine_.setRepeatMode(settings_.repeatMode);
    engine_.setShuffleMode(settings_.shuffleMode);

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
    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, nullptr);
    mouseinterval(0);
    printf("\033[?1003h");
    fflush(stdout);
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
        // Check for SGR mouse scroll events that ncurses doesn't decode on macOS
        if (key == KEY_MOUSE) {
            // Try ncurses mouse handling first
            handleInput(key);
        } else if (key == 27) {
            // Possible start of SGR sequence — peek next chars
            timeout(0);
            const int next1 = getch();
            if (next1 == '[') {
                timeout(0);
                const int next2 = getch();
                if (next2 == '<') {
                    // Read SGR mouse: \033[<X;Y;M
                    std::string seq;
                    timeout(0);
                    int ch;
                    while ((ch = getch()) != ERR && ch != 'M' && ch != 'm') {
                        if ((ch >= '0' && ch <= '9') || ch == ';') {
                            seq += static_cast<char>(ch);
                        }
                    }
                    if (ch == 'M') {
                        // Parse "btn;x;y"
                        int btn = 0, x = 0, y = 0;
                        if (std::sscanf(seq.c_str(), "%d;%d;%d", &btn, &x, &y) >= 1) {
                            if ((btn & 0x40) == 0) {
                                // Press event (bit 6 = 0)
                                if (btn == 0 || btn == 1 || btn == 2) {
                                    // Regular click — ignore, ncurses handles this
                                }
                            }
                            if (btn == 64) { // wheel up (64-65)
                                if (selectedIndex_ > 0) {
                                    selectedIndex_ = selectedIndex_ > 2 ? selectedIndex_ - 2 : 0;
                                }
                            } else if (btn == 65) { // wheel down
                                if (selectedIndex_ + 1 < engine_.playlist().size()) {
                                    selectedIndex_ = std::min(selectedIndex_ + 2, engine_.playlist().size() - 1);
                                }
                            }
                        }
                    }
                } else if (next2 != ERR) {
                    ungetch(next2);
                }
            } else if (next1 != ERR) {
                ungetch(next1);
                // If it's a standalone Esc, treat normally
                handleInput(key);
            }
        } else {
            handleInput(key);
        }
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
    printf("\033[?1003l");
    fflush(stdout);
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
    const int timeHeight = albumHeight >= 10 ? 4 : (albumHeight >= 2 ? 2 : 1);
    const int gapHeight = albumHeight >= 6 ? 1 : 0;
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
    attron(COLOR_PAIR(1) | A_BOLD);
    mvprintw(rect.top, rect.left + 2, "RetroWave");
    attroff(COLOR_PAIR(1) | A_BOLD);
    mvprintw(rect.top, rect.left + 14, "Terminal player with ASCII album art and .lrc lyrics");

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

    const bool overlayVisible = !activeError_.empty() || settingsOpen_ || !modalBody_.empty();
    if (!overlayVisible && overlayVisibleLastFrame_) {
        needsFullRedraw_ = true;
    }
    overlayVisibleLastFrame_ = overlayVisible;

    if (overlayVisible) {
        if (settingsOpen_) {
            wnoutrefresh(stdscr);
            drawSettingsOverlay(rows, cols);
            doupdate();
            return;
        }

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
        const int activeLyricIndex =
            snapshot.lyrics ? currentLyricIndex(*snapshot.lyrics, snapshot.positionSeconds) : -1;

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
    lastPlaybackState_ = snapshot.state;
    lastLyricsFound_ = snapshot.lyrics && snapshot.lyrics->found;
    lastVolumePercent_ = static_cast<int>(std::round(snapshot.volume * 100.0F));
    lastRepeatMode_ = snapshot.repeatMode;
    lastShuffleMode_ = snapshot.shuffleMode;
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
    const auto artLines = renderCoverArt(snapshot.albumArt, width, height, settings_.coverArtMode);
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
    lines.push_back(std::string("Repeat : ") + repeatModeLabel(snapshot.repeatMode));
    lines.push_back(std::string("Shuffle: ") + shuffleModeLabel(snapshot.shuffleMode));
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

    if (height >= 4) {
        // Four-row layout: label, double-height bar, time text
        attron(A_DIM);
        drawTextLine(top, left, width, "Time");
        attroff(A_DIM);

        const int barWidth = std::max(4, width - 2);
        if (snapshot.hasTrack && snapshot.durationSeconds > 0.0 && !snapshot.live) {
            const double fraction = std::clamp(snapshot.positionSeconds / snapshot.durationSeconds, 0.0, 1.0);
            const int filled = static_cast<int>(std::round(fraction * static_cast<double>(barWidth)));
            // Row 1: top half of double-thick bar with arrowhead
            mvaddch(top + 1, left, '[');
            mvaddch(top + 1, left + barWidth + 1, ']');
            for (int i = 0; i < barWidth; ++i) {
                if (i < filled) {
                    mvaddch(top + 1, left + 1 + i, i == filled - 1 && filled < barWidth ? '>' : '=');
                } else {
                    mvaddch(top + 1, left + 1 + i, '-');
                }
            }
            // Row 2: bottom half of double-thick bar (same, no arrowhead)
            mvaddch(top + 2, left, '[');
            mvaddch(top + 2, left + barWidth + 1, ']');
            for (int i = 0; i < barWidth; ++i) {
                if (i < filled) {
                    mvaddch(top + 2, left + 1 + i, '=');
                } else {
                    mvaddch(top + 2, left + 1 + i, '-');
                }
            }
            // Row 3: time text
            attron(A_BOLD);
            drawTextLine(top + 3, left, width, formatTime(snapshot.positionSeconds) + " / " + formatDuration(snapshot.durationSeconds, snapshot.live));
            attroff(A_BOLD);
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
            mvaddch(top + 1, left, '[');
            mvaddch(top + 1, left + barWidth + 1, ']');
            for (int i = 0; i < barWidth; ++i) {
                if (i < filled) {
                    mvaddch(top + 1, left + 1 + i, i == filled - 1 && filled < barWidth ? '>' : '=');
                } else {
                    mvaddch(top + 1, left + 1 + i, '-');
                }
            }
        } else if (snapshot.live) {
            drawTextLine(top + 1, left, width, "[ LIVE (seek unavailable) ]");
        } else {
            drawTextLine(top + 1, left, width, "[ no track ]");
        }

        if (height >= 3) {
            attron(A_BOLD);
            drawTextLine(top + 2, left, width, formatTime(snapshot.positionSeconds) + " / " + formatDuration(snapshot.durationSeconds, snapshot.live));
            attroff(A_BOLD);
        }
        return;
    }

    attron(A_BOLD);
    drawTextLine(top, left, width, "Time : " + formatTime(snapshot.positionSeconds) + " / " + formatDuration(snapshot.durationSeconds, snapshot.live));
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
        if (has_colors()) {
            attron(A_DIM);
        }
        drawTextLine(top + height / 2, left, width, message);
        if (has_colors()) {
            attroff(A_DIM);
        }
        return;
    }

    if (snapshot.lyrics->lines.empty()) {
        if (has_colors()) {
            attron(A_DIM);
        }
        drawTextLine(top + height / 2, left, width, snapshot.lyrics->message);
        if (has_colors()) {
            attroff(A_DIM);
        }
        return;
    }

    const int statusRows = 2;
    const int bodyRows = std::max(1, height - statusRows);
    const int activeIndex = currentLyricIndex(*snapshot.lyrics, snapshot.positionSeconds);

    int firstVisible = 0;
    if (snapshot.lyrics->timed && activeIndex >= 0) {
        firstVisible = std::max(0, activeIndex - std::max(1, bodyRows / 3));
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
        const bool isActive = lineIndex == activeIndex;
        const bool isNext = snapshot.lyrics->timed && lineIndex == activeIndex + 1;
        const bool isPast = snapshot.lyrics->timed && activeIndex >= 0 && lineIndex < activeIndex;
        const int distFromActive = isPast ? (activeIndex - lineIndex) : 0;

        if (isActive && snapshot.lyrics->enhanced && lyric.hasWordSync()) {
            const int segIdx = activeSegmentIndex(lyric, snapshot.positionSeconds);
            const std::string indicator = "▸ " + prefix;

            if (has_colors()) {
                attron(COLOR_PAIR(3) | A_BOLD);
            } else {
                attron(A_BOLD);
            }
            mvprintw(top + row, left, "%s", trimText(indicator, width).c_str());
            if (has_colors()) {
                attroff(COLOR_PAIR(3) | A_BOLD);
            } else {
                attroff(A_BOLD);
            }

            int col = left + static_cast<int>(indicator.size());
            const int maxCol = left + width;
            for (int si = 0; si < static_cast<int>(lyric.segments.size()) && col < maxCol; ++si) {
                const auto& seg = lyric.segments[static_cast<std::size_t>(si)];
                const std::string word = seg.text + " ";
                const bool segDone = si < segIdx;
                const bool segActive = si == segIdx;

                if (segActive) {
                    if (has_colors()) {
                        attron(COLOR_PAIR(3) | A_BOLD);
                    } else {
                        attron(A_BOLD | A_UNDERLINE);
                    }
                } else if (segDone) {
                    if (has_colors()) {
                        attron(COLOR_PAIR(2));
                    }
                } else {
                    if (has_colors()) {
                        attron(A_DIM);
                    }
                }

                const int remaining = maxCol - col;
                const std::string clipped = word.substr(0, static_cast<std::size_t>(std::max(0, remaining)));
                mvprintw(top + row, col, "%s", clipped.c_str());
                col += static_cast<int>(clipped.size());

                if (segActive) {
                    if (has_colors()) {
                        attroff(COLOR_PAIR(3) | A_BOLD);
                    } else {
                        attroff(A_BOLD | A_UNDERLINE);
                    }
                } else if (segDone) {
                    if (has_colors()) {
                        attroff(COLOR_PAIR(2));
                    }
                } else {
                    if (has_colors()) {
                        attroff(A_DIM);
                    }
                }
            }
        } else if (isActive) {
            const std::string indicator = "▸ ";
            const std::string rendered = trimText(indicator + prefix + lyric.text, width);

            if (has_colors()) {
                attron(COLOR_PAIR(3) | A_BOLD);
            } else {
                attron(A_BOLD);
            }
            drawTextLine(top + row, left, width, rendered);
            if (has_colors()) {
                attroff(COLOR_PAIR(3) | A_BOLD);
            } else {
                attroff(A_BOLD);
            }

            if (snapshot.lyrics->timed && row + 1 < bodyRows) {
                const double progress = lineProgress(lyric, snapshot.positionSeconds);
                const int barWidth = std::max(0, width - 2);
                const int filled = static_cast<int>(std::round(progress * barWidth));

                mvhline(top + row + 1, left, ' ', width);
                if (barWidth > 0 && has_colors()) {
                    attron(COLOR_PAIR(4));
                    for (int b = 0; b < barWidth; ++b) {
                        mvaddch(top + row + 1, left + 1 + b, b < filled ? ACS_HLINE : ' ');
                    }
                    attroff(COLOR_PAIR(4));
                }

                ++row;
            }
        } else if (isNext) {
            const std::string rendered = trimText("  " + prefix + lyric.text, width);
            if (has_colors()) {
                attron(COLOR_PAIR(1));
            }
            drawTextLine(top + row, left, width, rendered);
            if (has_colors()) {
                attroff(COLOR_PAIR(1));
            }
        } else if (isPast) {
            const std::string rendered = trimText("  " + prefix + lyric.text, width);
            attron(A_DIM);
            if (distFromActive <= 2 && has_colors()) {
                attron(COLOR_PAIR(2));
            }
            drawTextLine(top + row, left, width, rendered);
            if (distFromActive <= 2 && has_colors()) {
                attroff(COLOR_PAIR(2));
            }
            attroff(A_DIM);
        } else {
            const std::string rendered = trimText("  " + prefix + lyric.text, width);
            drawTextLine(top + row, left, width, rendered);
        }
    }

    std::string statusText = snapshot.lyrics->message;
    if (snapshot.lyrics->timed && activeIndex >= 0) {
        const auto& activeLine = snapshot.lyrics->lines[static_cast<std::size_t>(activeIndex)];
        const double progress = lineProgress(activeLine, snapshot.positionSeconds);
        const int pct = static_cast<int>(std::round(progress * 100.0));

        std::string typeLabel = snapshot.lyrics->enhanced ? "enhanced" : "synced";
        statusText = statusText + "  " + std::to_string(activeIndex + 1) + "/" +
                     std::to_string(snapshot.lyrics->lines.size()) + "  " + typeLabel + "  " +
                     std::to_string(pct) + "%";
    }
    attron(A_DIM);
    drawTextLine(top + height - 1, left, width, statusText);
    attroff(A_DIM);
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

void TerminalUI::drawSettingsOverlay(int rows, int cols) const {
    const int overlayWidth = std::min(cols - 8, 64);
    const int overlayHeight = 13;
    const int top = std::max(2, (rows - overlayHeight) / 2);
    const int left = std::max(2, (cols - overlayWidth) / 2);

    WINDOW* overlay = newwin(overlayHeight, overlayWidth, top, left);
    if (overlay == nullptr) {
        return;
    }

    if (has_colors()) {
        wbkgd(overlay, COLOR_PAIR(6) | ' ');
    } else {
        wbkgd(overlay, A_REVERSE);
    }
    werase(overlay);
    box(overlay, 0, 0);

    wattron(overlay, A_BOLD);
    mvwprintw(overlay, 0, 2, " Settings ");
    wattroff(overlay, A_BOLD);

    mvwaddnstr(overlay, 2, 2, "Cover renderer", -1);
    if (has_colors()) {
        wattron(overlay, COLOR_PAIR(5) | A_BOLD);
    } else {
        wattron(overlay, A_BOLD);
    }
    mvwprintw(overlay, 4, 4, "< %s >", coverArtModeLabel(settings_.coverArtMode));
    if (has_colors()) {
        wattroff(overlay, COLOR_PAIR(5) | A_BOLD);
    } else {
        wattroff(overlay, A_BOLD);
    }

    mvwprintw(overlay, 6, 2, "Volume is saved automatically: %3d%%", static_cast<int>(std::round(settings_.volume * 100.0F)));
    mvwprintw(overlay, 7, 2, "Repeat mode: %s", repeatModeLabel(settings_.repeatMode));
    mvwprintw(overlay, 8, 2, "Shuffle mode: %s", shuffleModeLabel(settings_.shuffleMode));
    mvwaddnstr(overlay, 10, 2, "Left/Right switch renderer   r repeat mode   h shuffle mode", -1);
    mvwaddnstr(overlay, 10, 2, "Enter/Esc close", -1);

    wnoutrefresh(overlay);
    delwin(overlay);
}

void TerminalUI::handleInput(int key) {
    if (!activeError_.empty() || settingsOpen_ || !modalBody_.empty()) {
        if (settingsOpen_) {
            switch (key) {
                case 'q':
                case 'Q':
                    running_ = false;
                    return;
                case 27:
                case '\n':
                case KEY_ENTER:
                case 's':
                case 'S':
                    closeTransientOverlay();
                    return;
                case KEY_LEFT:
                case '-':
                    cycleCoverArtMode(-1);
                    return;
                case KEY_RIGHT:
                case '+':
                case '=':
                    cycleCoverArtMode(1);
                    return;
                case 'r':
                case 'R':
                    cycleRepeatMode();
                    return;
                case 'h':
                case 'H':
                    cycleShuffleMode();
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
            case 27:
            case '\n':
            case KEY_ENTER:
            case ' ':
                closeTransientOverlay();
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
            engine_.next();
            selectedIndex_ = engine_.snapshot().currentIndex;
            return;
        case 'p':
        case 'P':
            engine_.previous();
            selectedIndex_ = engine_.snapshot().currentIndex;
            return;
        case KEY_PPAGE: {
            const auto snap = engine_.snapshot();
            engine_.seek(snap.positionSeconds - 10.0);
            return;
        }
        case KEY_NPAGE: {
            const auto snap = engine_.snapshot();
            engine_.seek(snap.positionSeconds + 10.0);
            return;
        }
        case '<':
        case ',':
            engine_.seek(engine_.snapshot().positionSeconds - 5.0);
            return;
        case '>':
        case '.':
            engine_.seek(engine_.snapshot().positionSeconds + 5.0);
            return;
        case '+':
        case '=':
            engine_.adjustVolume(0.05F);
            settings_.volume = engine_.snapshot().volume;
            persistSettings();
            return;
        case '-':
        case '_':
            engine_.adjustVolume(-0.05F);
            settings_.volume = engine_.snapshot().volume;
            persistSettings();
            return;
        case 't':
        case 'T':
            detailMode_ = detailMode_ == DetailMode::Visualizer ? DetailMode::Lyrics : DetailMode::Visualizer;
            return;
        case KEY_MOUSE: {
            MEVENT event;
            if (getmouse(&event) != OK) {
                return;
            }
            if (event.bstate & BUTTON1_CLICKED) {
                // Playlist click: whole frame area — each row = one track
                if (event.y >= layout_.playlistFrame.top + 1 &&
                    event.y < layout_.playlistFrame.top + layout_.playlistFrame.height - 1 &&
                    event.x >= layout_.playlistFrame.left + 1 &&
                    event.x < layout_.playlistFrame.left + layout_.playlistFrame.width - 1) {
                    const int trackCount = static_cast<int>(engine_.playlist().size());
                    const int contentRows = layout_.playlistFrame.height - 2;
                    const int clickedRow = event.y - (layout_.playlistFrame.top + 1);
                    // Map row to track index (with scrolling)
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
        case 'r':
        case 'R':
            cycleRepeatMode();
            return;
        case 'h':
        case 'H':
            cycleShuffleMode();
            return;
        case 's':
        case 'S':
            openSettingsOverlay();
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

}  // namespace retrowave