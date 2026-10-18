// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#pragma once

namespace retrowave {

enum class DetailMode {
    Visualizer,
    Lyrics
};

enum class LayoutPreset {
    Default,
    VisualizerFocus,
    LyricsFocus,
    Compact
};

struct Rect {
    int top = 0;
    int left = 0;
    int height = 0;
    int width = 0;
};

struct Layout {
    int rows = 0;
    int cols = 0;
    Rect header;
    Rect playlistFrame;
    Rect playlistContent;
    Rect albumFrame;
    Rect cover;
    Rect meta;
    Rect time;
    Rect detailFrame;
    Rect detailContent;
};

class LayoutEngine {
public:
    [[nodiscard]] static Layout computeLayout(int rows, int cols, LayoutPreset preset);
};

} // namespace retrowave
