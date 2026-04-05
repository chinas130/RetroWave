#include "ui/TerminalUI.h"

#include <ncurses.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace retrowave {
namespace {

std::string trimText(const std::string& value, int width) {
    if (width <= 0) {
        return {};
    }

    if (static_cast<int>(value.size()) <= width) {
        return value;
    }

    if (width <= 3) {
        return value.substr(0, static_cast<std::size_t>(width));
    }

    return value.substr(0, static_cast<std::size_t>(width - 3)) + "...";
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

std::string makeBar(double ratio, int width, char fill = '#', char empty = '-') {
    ratio = std::clamp(ratio, 0.0, 1.0);
    const int filled = static_cast<int>(std::round(ratio * width));

    std::string bar(static_cast<std::size_t>(width), empty);
    for (int index = 0; index < filled && index < width; ++index) {
        bar[static_cast<std::size_t>(index)] = fill;
    }
    return bar;
}

std::vector<std::string> wrapText(const std::string& text, int width) {
    std::vector<std::string> lines;
    if (width <= 0) {
        return lines;
    }

    std::istringstream words(text);
    std::string word;
    std::string currentLine;

    auto flushLine = [&]() {
        if (!currentLine.empty()) {
            lines.push_back(currentLine);
            currentLine.clear();
        }
    };

    while (words >> word) {
        while (static_cast<int>(word.size()) > width) {
            if (!currentLine.empty()) {
                flushLine();
            }
            lines.push_back(word.substr(0, static_cast<std::size_t>(width)));
            word.erase(0, static_cast<std::size_t>(width));
        }

        if (currentLine.empty()) {
            currentLine = word;
            continue;
        }

        if (static_cast<int>(currentLine.size() + 1 + word.size()) <= width) {
            currentLine += ' ';
            currentLine += word;
            continue;
        }

        flushLine();
        currentLine = word;
    }

    flushLine();

    if (lines.empty()) {
        lines.push_back({});
    }

    return lines;
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
    timeout(80);

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
        const PlaybackSnapshot snapshot = engine_.snapshot();
        syncErrorOverlay(snapshot);

        const int key = getch();
        if (key != ERR) {
            handleInput(key);
        }

        draw(snapshot);
    }

    endwin();
    return 0;
}

void TerminalUI::draw(const PlaybackSnapshot& snapshot) const {
    erase();

    int rows = 0;
    int cols = 0;
    getmaxyx(stdscr, rows, cols);

    const int minWidth = 96;
    const int minHeight = 24;
    if (rows < minHeight || cols < minWidth) {
        mvprintw(1, 2, "RetroWave needs at least %dx%d terminal size.", minWidth, minHeight);
        mvprintw(3, 2, "Current size: %dx%d", cols, rows);
        mvprintw(5, 2, "Resize the terminal to continue.");
        refresh();
        return;
    }

    const int playlistWidth = std::max(32, cols / 3);
    const int rightWidth = cols - playlistWidth - 3;
    const int contentHeight = rows - 2;
    const int detailHeight = std::max(9, contentHeight / 2);
    const int cardHeight = contentHeight - detailHeight;

    attron(COLOR_PAIR(1) | A_BOLD);
    mvprintw(0, 2, "RetroWave");
    attroff(COLOR_PAIR(1) | A_BOLD);
    mvprintw(0, 14, "Terminal player with ASCII album art and .lrc lyrics");

    drawFrame(1, 1, contentHeight, playlistWidth, "Playlist");
    drawFrame(1, playlistWidth + 2, cardHeight, rightWidth, "Album Card");

    const char* detailTitle = detailMode_ == DetailMode::Lyrics ? "Lyrics" : "Visualizer";
    drawFrame(1 + cardHeight, playlistWidth + 2, detailHeight, rightWidth, detailTitle);

    drawPlaylist(2, 2, contentHeight - 2, playlistWidth - 2, snapshot);
    drawAlbumCard(2, playlistWidth + 3, cardHeight - 2, rightWidth - 2, snapshot);

    if (detailMode_ == DetailMode::Lyrics) {
        drawLyrics(2 + cardHeight, playlistWidth + 3, detailHeight - 2, rightWidth - 2, snapshot);
    } else {
        drawVisualizer(2 + cardHeight, playlistWidth + 3, detailHeight - 2, rightWidth - 2, snapshot);
    }

    if (!activeError_.empty()) {
        drawModalOverlay(rows, cols, "Error", "Playback backend or decode failure", activeError_);
    } else if (!modalBody_.empty()) {
        drawLegalScreen(rows, cols);
    }

    refresh();
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
    const auto& items = engine_.playlist().items();
    if (items.empty()) {
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
        if (index >= items.size()) {
            break;
        }

        const bool isSelected = index == selectedIndex_;
        const bool isCurrent = snapshot.hasTrack && index == snapshot.currentIndex;
        const std::string prefix = isCurrent ? "> " : "  ";
        const std::string line = prefix + trimText(items[index].title, width - 4);

        if (isSelected) {
            attron(A_REVERSE);
        }
        if (isCurrent && has_colors()) {
            attron(COLOR_PAIR(2) | A_BOLD);
        }

        mvprintw(top + row, left, "%-*s", width, line.c_str());

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
    const auto artLines = renderAsciiArt(snapshot.albumArt, artWidth, height);
    const int artTop = top + std::max(0, (height - static_cast<int>(artLines.size())) / 2);

    for (int row = 0; row < height; ++row) {
        mvprintw(top + row, left, "%-*s", artWidth, "");
    }

    for (int row = 0; row < std::min(height, static_cast<int>(artLines.size())); ++row) {
        const auto& line = artLines[static_cast<std::size_t>(row)];
        const int artLeft = left + std::max(0, (artWidth - static_cast<int>(line.size())) / 2);
        if (has_colors()) {
            attron(COLOR_PAIR(4));
        }
        mvprintw(artTop + row, artLeft, "%s", line.c_str());
        if (has_colors()) {
            attroff(COLOR_PAIR(4));
        }
    }

    attron(A_BOLD);
    mvprintw(top, infoLeft, "%s", trimText(snapshot.title, infoWidth).c_str());
    attroff(A_BOLD);

    mvprintw(top + 2, infoLeft, "Artist : %s", trimText(snapshot.artist.empty() ? "Unknown" : snapshot.artist, infoWidth - 9).c_str());
    mvprintw(top + 3, infoLeft, "Album  : %s", trimText(snapshot.album.empty() ? "Unknown" : snapshot.album, infoWidth - 9).c_str());

    const std::string state = snapshot.loading ? "loading" : (snapshot.paused ? "paused" : "playing");
    mvprintw(top + 5, infoLeft, "State  : %s", state.c_str());
    mvprintw(
        top + 6,
        infoLeft,
        "Time   : %s / %s",
        formatTime(snapshot.positionSeconds).c_str(),
        formatTime(snapshot.durationSeconds).c_str());
    mvprintw(top + 7, infoLeft, "Volume : %3d%%", static_cast<int>(std::round(snapshot.volume * 100.0F)));
    mvprintw(top + 8, infoLeft, "Level  : [%s]", makeBar(snapshot.level, std::max(8, infoWidth - 11), '=', '.').c_str());

    const std::string lyricsStatus =
        snapshot.lyrics && snapshot.lyrics->found ? "loaded" : "missing";
    mvprintw(top + 10, infoLeft, "Lyrics : %s", lyricsStatus.c_str());
    mvprintw(top + 11, infoLeft, "Source : %s", trimText(snapshot.path, infoWidth - 9).c_str());
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
        mvprintw(top + height / 2, left, "%s", trimText(message, width).c_str());
        return;
    }

    if (snapshot.lyrics->lines.empty()) {
        mvprintw(top + height / 2, left, "%s", trimText(snapshot.lyrics->message, width).c_str());
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

        mvprintw(top + row, left, "%-*s", width, rendered.c_str());

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

    mvprintw(top + height - 1, left, "%s", trimText(snapshot.lyrics->message, width).c_str());
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
    mvwprintw(overlay, 1, 2, "%s", trimText(subtitle, overlayWidth - 4).c_str());
    wattroff(overlay, A_BOLD);

    for (int index = 0; index < messageLines; ++index) {
        mvwprintw(
            overlay,
            2 + index,
            2,
            "%-*s",
            overlayWidth - 4,
            wrappedBody[static_cast<std::size_t>(index)].c_str());
    }

    if (static_cast<int>(wrappedBody.size()) > messageLines) {
        mvwprintw(overlay, 2 + messageLines - 1, overlayWidth - 6, "%s", "...");
    }

    if (has_colors()) {
        wattron(overlay, COLOR_PAIR(6) | A_BOLD);
    } else {
        wattron(overlay, A_BOLD);
    }
    mvwprintw(overlay, overlayHeight - 2, 2, "%-*s", overlayWidth - 4, "Enter/Esc dismiss   q quit");
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
    mvprintw(boxTop + 2, boxLeft + 3, "%s", trimText(modalSubtitle_, bodyWidth).c_str());
    if (has_colors()) {
        attroff(COLOR_PAIR(3) | A_BOLD);
    } else {
        attroff(A_BOLD);
    }

    for (int index = 0; index < std::min<int>(wrappedBody.size(), maxBodyLines); ++index) {
        mvprintw(
            boxTop + 4 + index,
            boxLeft + 3,
            "%-*s",
            bodyWidth,
            wrappedBody[static_cast<std::size_t>(index)].c_str());
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
