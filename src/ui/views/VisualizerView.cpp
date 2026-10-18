// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#include "ui/views/VisualizerView.h"
#include <ncurses.h>
#include <algorithm>
#include <cmath>

namespace retrowave {

namespace {
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
}

void VisualizerView::clearState() {
    visualizerState_.clear();
    visualizerTargets_.clear();
}

void VisualizerView::render(const Rect& rect, const PlaybackSnapshot& snapshot, const Theme& theme) {
    if (rect.width <= 0 || rect.height <= 0) return;
    if (!snapshot.hasTrack || snapshot.visualizer.empty() || rect.height < 4 || rect.width < 8) {
        mvprintw(rect.top, rect.left, "Visualizer will appear when a track is loaded.");
        return;
    }

    visualizerState_.resize(static_cast<std::size_t>(rect.width), 0.0F);
    visualizerTargets_.resize(static_cast<std::size_t>(rect.width), 0.0F);
    ++visualizerTick_;

    const int barHeight = rect.height - 2;
    const double duration = std::max(0.001, snapshot.durationSeconds);
    const double progress = std::clamp(snapshot.positionSeconds / duration, 0.0, 1.0);

    float localPeak = 0.0F;

    for (int column = 0; column < rect.width; ++column) {
        const double position = static_cast<double>(column) / std::max(1, rect.width - 1);
        const double animatedOffset = std::fmod(
            progress + position * 0.55 + static_cast<double>(visualizerTick_ % 48) / 96.0,
            1.0);
        const float amplitude = sampleVisualizer(snapshot.visualizer, animatedOffset);
        visualizerTargets_[static_cast<std::size_t>(column)] = amplitude;
        localPeak = std::max(localPeak, amplitude);
    }

    const float autoGain = localPeak > 0.0001F ? 1.0F / localPeak : 1.0F;
    for (int column = 0; column < rect.width; ++column) {
        auto normalized = std::clamp(visualizerTargets_[static_cast<std::size_t>(column)] * autoGain, 0.0F, 1.0F);
        normalized = std::pow(normalized, 0.72F);
        normalized = std::clamp(normalized * (0.65F + snapshot.level * 0.8F), 0.0F, 1.0F);

        auto& state = visualizerState_[static_cast<std::size_t>(column)];
        if (normalized > state) {
            state = normalized;
        } else {
            state = std::max(normalized, state * 0.86F - 0.01F);
        }

        const float exactFilled = state * static_cast<float>(barHeight);
        const int filled = std::clamp(static_cast<int>(std::ceil(exactFilled)), 0, barHeight);

        for (int row = 0; row < barHeight; ++row) {
            const int screenRow = rect.top + barHeight - 1 - row;
            
            int colorToken = static_cast<int>(ColorToken::VizBarLow);
            if (row >= barHeight * 2 / 3) colorToken = static_cast<int>(ColorToken::VizBarHigh);
            else if (row >= barHeight / 3) colorToken = static_cast<int>(ColorToken::VizBarMid);
            
            if (row == filled - 1) colorToken = static_cast<int>(ColorToken::VizPeak);

            if (row < filled) {
                if (has_colors()) attron(COLOR_PAIR(colorToken) | A_BOLD);
                
                if (theme.vizStyle == VizStyle::Ascii) {
                    mvaddch(screenRow, rect.left + column, row == filled - 1 ? '#' : '|');
                } else if (theme.vizStyle == VizStyle::Braille) {
                    static const char* braille[] = {"⡀","⣀","⣄","⣤","⣦","⣶","⣷","⣿"};
                    if (row == filled - 1) {
                        float frac = exactFilled - std::floor(exactFilled);
                        if (frac == 0.0f && exactFilled > 0.0f) frac = 1.0f;
                        int idx = std::clamp(static_cast<int>(frac * 8.0f), 0, 7);
                        mvprintw(screenRow, rect.left + column, "%s", braille[idx]);
                    } else {
                        mvprintw(screenRow, rect.left + column, "⣿");
                    }
                } else {
                    static const char* blocks[] = {" ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
                    if (row == filled - 1) {
                        float frac = exactFilled - std::floor(exactFilled);
                        if (frac == 0.0f && exactFilled > 0.0f) frac = 1.0f;
                        int idx = std::clamp(static_cast<int>(frac * 8.0f), 0, 7);
                        mvprintw(screenRow, rect.left + column, "%s", blocks[idx]);
                    } else {
                        mvprintw(screenRow, rect.left + column, "█");
                    }
                }
                
                if (has_colors()) attroff(COLOR_PAIR(colorToken) | A_BOLD);
            } else if (row == 0) {
                if (theme.vizStyle == VizStyle::Ascii) {
                    mvaddch(screenRow, rect.left + column, '.');
                } else {
                    mvprintw(screenRow, rect.left + column, " ");
                }
            }
        }
    }

    mvprintw(rect.top + rect.height - 1, rect.left, "Auto gain x%.1f   level %3d%%", autoGain, static_cast<int>(std::round(snapshot.level * 100.0F)));
}

} // namespace retrowave
