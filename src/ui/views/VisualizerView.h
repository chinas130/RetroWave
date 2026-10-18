// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#pragma once

#include "ui/LayoutEngine.h"
#include "audio/PlaybackEngine.h"
#include "ui/Theme.h"
#include <vector>

namespace retrowave {

class VisualizerView {
public:
    void render(const Rect& rect, const PlaybackSnapshot& snapshot, const Theme& theme);
    void clearState();

private:
    std::vector<float> visualizerState_;
    std::vector<float> visualizerTargets_;
    std::size_t visualizerTick_ = 0;
};

} // namespace retrowave
