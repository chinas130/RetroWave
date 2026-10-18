// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#pragma once

#include "core/Settings.h"
#include "audio/PlaybackEngine.h"

#include <string>
#include <vector>
#include <memory>

namespace retrowave {

class CoverArtView {
public:
    static std::vector<std::string> renderPlaceholderArt(int width, int height);
    static std::vector<float> sampleAlbumArt(
        const std::shared_ptr<const AlbumArt>& art,
        int targetWidth,
        int targetHeight,
        float& minGray,
        float& maxGray);
    static std::vector<std::string> renderCoverArt(
        const std::shared_ptr<const AlbumArt>& art,
        int maxWidth,
        int maxHeight,
        CoverArtMode mode);
};

} // namespace retrowave
