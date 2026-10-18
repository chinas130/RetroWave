// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#include "ui/views/ViewUtils.h"
#include <ncurses.h>
#include <cwchar>
#include <cmath>
#include <algorithm>

namespace retrowave {

const char* coverArtModeLabel(CoverArtMode mode) {
    switch (mode) {
        case CoverArtMode::Ascii: return "ASCII";
        case CoverArtMode::BlockShading: return "Block Shading";
        case CoverArtMode::HalfBlock: return "Half-Block";
    }
    return "ASCII";
}

const char* repeatModeLabel(RepeatMode mode) {
    switch (mode) {
        case RepeatMode::Off: return "off";
        case RepeatMode::One: return "one";
        case RepeatMode::All: return "all";
    }
    return "off";
}

const char* shuffleModeLabel(ShuffleMode mode) {
    switch (mode) {
        case ShuffleMode::Off: return "off";
        case ShuffleMode::On: return "on";
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
        if (length == 0) break;
        total += columns;
        offset += length;
    }
    return total;
}

std::string takeColumns(const std::string& value, int width) {
    if (width <= 0) return {};
    std::size_t endOffset = 0;
    int consumed = 0;
    for (std::size_t offset = 0; offset < value.size();) {
        int columns = 0;
        const auto length = nextGlyphBytes(value, offset, columns);
        if (length == 0) break;
        if (consumed + columns > width) break;
        consumed += columns;
        offset += length;
        endOffset = offset;
    }
    return value.substr(0, endOffset);
}

std::string trimText(const std::string& value, int width) {
    if (width <= 0) return {};
    if (displayWidth(value) <= width) return value;
    if (width <= 3) return takeColumns(value, width);
    return takeColumns(value, width - 3) + "...";
}

std::vector<std::string> wrapText(const std::string& text, int width) {
    std::vector<std::string> lines;
    if (width <= 0) return lines;

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
    if (lines.empty()) lines.push_back({});
    return lines;
}

std::string formatTime(double seconds) {
    const int total = std::max(0, static_cast<int>(std::round(seconds)));
    const int minutes = total / 60;
    const int remainder = total % 60;
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d:%02d", minutes, remainder);
    return std::string(buffer);
}

std::string formatDuration(double seconds, bool live) {
    if (live || seconds <= 0.0) return "LIVE";
    return formatTime(seconds);
}

std::string playbackStateLabel(PlaybackState state) {
    switch (state) {
        case PlaybackState::Idle: return "idle";
        case PlaybackState::Loading: return "loading";
        case PlaybackState::Playing: return "playing";
        case PlaybackState::Paused: return "paused";
        case PlaybackState::Buffering: return "buffering";
        case PlaybackState::Ended: return "ended";
        case PlaybackState::Failed: return "failed";
    }
    return "unknown";
}

void drawTextLine(int row, int column, int width, const std::string& text) {
    if (width <= 0) return;
    mvhline(row, column, ' ', width);
    const auto clipped = trimText(text, width);
    if (!clipped.empty()) {
        mvaddnstr(row, column, clipped.c_str(), static_cast<int>(clipped.size()));
    }
}

} // namespace retrowave
