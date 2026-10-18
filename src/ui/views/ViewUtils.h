// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#pragma once

#include "audio/PlaybackEngine.h"
#include "core/Settings.h"
#include <string>
#include <vector>

namespace retrowave {

const char* coverArtModeLabel(CoverArtMode mode);
const char* repeatModeLabel(RepeatMode mode);
const char* shuffleModeLabel(ShuffleMode mode);

std::size_t nextGlyphBytes(const std::string& value, std::size_t offset, int& columns);
int displayWidth(const std::string& value);
std::string takeColumns(const std::string& value, int width);
std::string trimText(const std::string& value, int width);
std::vector<std::string> wrapText(const std::string& text, int width);

std::string formatTime(double seconds);
std::string formatDuration(double seconds, bool live);
std::string playbackStateLabel(PlaybackState state);

void drawTextLine(int row, int column, int width, const std::string& text);

} // namespace retrowave
