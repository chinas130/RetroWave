// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#pragma once

#include "ui/LayoutEngine.h"
#include "audio/PlaybackEngine.h"
#include "ui/Theme.h"

namespace retrowave {

class LyricsView {
public:
    void render(const Rect& rect, const PlaybackSnapshot& snapshot, LayoutPreset preset, const Theme& theme);
};

} // namespace retrowave
