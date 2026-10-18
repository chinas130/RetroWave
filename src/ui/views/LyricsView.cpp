// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#include "ui/views/LyricsView.h"
#include "ui/views/ViewUtils.h"
#include <ncurses.h>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace retrowave {

namespace {
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
} // namespace

void LyricsView::render(const Rect& rect, const PlaybackSnapshot& snapshot, LayoutPreset preset, const Theme& theme) {
    (void)theme; // Theme handled globally via color pairs
    const int top = rect.top;
    const int left = rect.left;
    const int height = rect.height;
    const int width = rect.width;

    if (width <= 0 || height <= 0) return;
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

        int renderLeft = left;
        int renderWidth = width;
        if (preset == LayoutPreset::LyricsFocus) {
            std::string fullText;
            if (isActive && snapshot.lyrics->enhanced && lyric.hasWordSync()) {
                fullText = "▸ " + prefix;
                for (const auto& seg : lyric.segments) {
                    fullText += seg.text + " ";
                }
            } else if (isActive) {
                fullText = "▸ " + prefix + lyric.text;
            } else {
                fullText = "  " + prefix + lyric.text;
            }
            int textWidth = displayWidth(fullText);
            if (textWidth < width) {
                renderLeft = left + (width - textWidth) / 2;
                renderWidth = width - (renderLeft - left);
            }
        }

        if (isActive && snapshot.lyrics->enhanced && lyric.hasWordSync()) {
            const int segIdx = activeSegmentIndex(lyric, snapshot.positionSeconds);
            const std::string indicator = "▸ " + prefix;

            if (has_colors()) {
                attron(COLOR_PAIR(static_cast<int>(ColorToken::LyricActive)) | A_BOLD);
            } else {
                attron(A_BOLD);
            }
            mvprintw(top + row, renderLeft, "%s", trimText(indicator, renderWidth).c_str());
            if (has_colors()) {
                attroff(COLOR_PAIR(static_cast<int>(ColorToken::LyricActive)) | A_BOLD);
            } else {
                attroff(A_BOLD);
            }

            int col = renderLeft + static_cast<int>(indicator.size());
            const int maxCol = renderLeft + renderWidth;
            for (int si = 0; si < static_cast<int>(lyric.segments.size()) && col < maxCol; ++si) {
                const auto& seg = lyric.segments[static_cast<std::size_t>(si)];
                const std::string word = seg.text + " ";
                const bool segDone = si < segIdx;
                const bool segActive = si == segIdx;

                if (segActive) {
                    if (has_colors()) {
                        attron(COLOR_PAIR(static_cast<int>(ColorToken::LyricActiveWord)) | A_BOLD);
                    } else {
                        attron(A_BOLD | A_UNDERLINE);
                    }
                } else if (segDone) {
                    if (has_colors()) {
                        attron(COLOR_PAIR(static_cast<int>(ColorToken::LyricPast)));
                    }
                } else {
                    if (has_colors()) {
                        attron(COLOR_PAIR(static_cast<int>(ColorToken::LyricNext)) | A_DIM);
                    } else {
                        attron(A_DIM);
                    }
                }

                const int remaining = maxCol - col;
                const std::string clipped = word.substr(0, static_cast<std::size_t>(std::max(0, remaining)));
                mvprintw(top + row, col, "%s", clipped.c_str());
                col += static_cast<int>(clipped.size());

                if (segActive) {
                    if (has_colors()) {
                        attroff(COLOR_PAIR(static_cast<int>(ColorToken::LyricActiveWord)) | A_BOLD);
                    } else {
                        attroff(A_BOLD | A_UNDERLINE);
                    }
                } else if (segDone) {
                    if (has_colors()) {
                        attroff(COLOR_PAIR(static_cast<int>(ColorToken::LyricPast)));
                    }
                } else {
                    if (has_colors()) {
                        attroff(COLOR_PAIR(static_cast<int>(ColorToken::LyricNext)) | A_DIM);
                    } else {
                        attroff(A_DIM);
                    }
                }
            }
        } else if (isActive) {
            const std::string indicator = "▸ ";
            const std::string rendered = trimText(indicator + prefix + lyric.text, renderWidth);

            if (has_colors()) {
                attron(COLOR_PAIR(static_cast<int>(ColorToken::LyricActive)) | A_BOLD);
            } else {
                attron(A_BOLD);
            }
            drawTextLine(top + row, renderLeft, renderWidth, rendered);
            if (has_colors()) {
                attroff(COLOR_PAIR(static_cast<int>(ColorToken::LyricActive)) | A_BOLD);
            } else {
                attroff(A_BOLD);
            }

            if (snapshot.lyrics->timed && row + 1 < bodyRows) {
                const double progress = lineProgress(lyric, snapshot.positionSeconds);
                const int barWidth = std::max(0, renderWidth - 2);
                const int filled = static_cast<int>(std::round(progress * barWidth));

                mvhline(top + row + 1, renderLeft, ' ', renderWidth);
                if (barWidth > 0 && has_colors()) {
                    attron(COLOR_PAIR(static_cast<int>(ColorToken::TimeBarFilled)));
                    for (int b = 0; b < barWidth; ++b) {
                        mvaddch(top + row + 1, renderLeft + 1 + b, b < filled ? ACS_HLINE : ' ');
                    }
                    attroff(COLOR_PAIR(static_cast<int>(ColorToken::TimeBarFilled)));
                }

                ++row;
            }
        } else if (isNext) {
            const std::string rendered = trimText("  " + prefix + lyric.text, renderWidth);
            if (has_colors()) attron(COLOR_PAIR(static_cast<int>(ColorToken::LyricNext)));
            drawTextLine(top + row, renderLeft, renderWidth, rendered);
            if (has_colors()) attroff(COLOR_PAIR(static_cast<int>(ColorToken::LyricNext)));
        } else if (isPast) {
            const std::string rendered = trimText("  " + prefix + lyric.text, renderWidth);
            if (has_colors()) attron(COLOR_PAIR(static_cast<int>(ColorToken::LyricPast)) | A_DIM);
            else attron(A_DIM);
            drawTextLine(top + row, renderLeft, renderWidth, rendered);
            if (has_colors()) attroff(COLOR_PAIR(static_cast<int>(ColorToken::LyricPast)) | A_DIM);
            else attroff(A_DIM);
        } else {
            const std::string rendered = trimText("  " + prefix + lyric.text, renderWidth);
            if (has_colors()) attron(COLOR_PAIR(static_cast<int>(ColorToken::LyricNormal)));
            drawTextLine(top + row, renderLeft, renderWidth, rendered);
            if (has_colors()) attroff(COLOR_PAIR(static_cast<int>(ColorToken::LyricNormal)));
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

} // namespace retrowave
