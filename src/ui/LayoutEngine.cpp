// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#include "ui/LayoutEngine.h"
#include <algorithm>

namespace retrowave {

Layout LayoutEngine::computeLayout(int rows, int cols, LayoutPreset preset) {
    Layout layout;
    layout.rows = rows;
    layout.cols = cols;
    layout.header = {0, 0, 1, cols};

    if (preset == LayoutPreset::Compact) {
        layout.meta = {1, 1, 1, cols - 2};
        layout.time = {2, 1, 2, cols - 2};
        layout.detailContent = {4, 1, std::max(0, rows - 5), cols - 2};
        return layout;
    }
    
    if (preset == LayoutPreset::VisualizerFocus) {
        layout.meta = {1, 1, 2, cols - 2};
        layout.time = {3, 1, 2, cols - 2};
        layout.detailFrame = {5, 0, rows - 5, cols};
        layout.detailContent = {6, 1, rows - 7, cols - 2};
        return layout;
    }
    
    if (preset == LayoutPreset::LyricsFocus) {
        layout.detailFrame = {1, 0, rows - 1, cols};
        layout.detailContent = {2, 1, rows - 3, cols - 2};
        return layout;
    }

    const int playlistWidth = (cols < 60) ? 0 : std::max(32, cols / 3);
    const int rightWidth = cols - playlistWidth - (playlistWidth > 0 ? 3 : 0);
    const int contentHeight = rows - 2;
    const int detailHeight = std::max(9, contentHeight / 2);
    const int cardHeight = contentHeight - detailHeight;

    if (playlistWidth > 0) {
        layout.playlistFrame = {1, 1, contentHeight, playlistWidth};
        layout.playlistContent = {2, 2, contentHeight - 2, playlistWidth - 2};
    }
    
    const int rightLeftOffset = playlistWidth > 0 ? playlistWidth + 2 : 1;
    layout.albumFrame = {1, rightLeftOffset, cardHeight, rightWidth};

    const int albumTop = 2;
    const int albumLeft = rightLeftOffset + 1;
    const int albumHeight = cardHeight - 2;
    const int albumWidth = rightWidth - 2;
    
    const int artWidth = (cols < 85) ? 0 : std::clamp(albumWidth / 3, 18, 28);
    layout.cover = {albumTop, albumLeft, albumHeight, artWidth};
    
    const int infoLeft = albumLeft + (artWidth > 0 ? artWidth + 2 : 0);
    const int infoWidth = std::max(12, albumWidth - artWidth - (artWidth > 0 ? 2 : 0));
    const int timeHeight = albumHeight >= 10 ? 4 : (albumHeight >= 2 ? 2 : 1);
    const int gapHeight = albumHeight >= 6 ? 1 : 0;
    const int metaHeight = std::max(1, albumHeight - timeHeight - gapHeight);

    layout.meta = {albumTop, infoLeft, metaHeight, infoWidth};
    layout.time = {albumTop + metaHeight + gapHeight, infoLeft, timeHeight, infoWidth};

    layout.detailFrame = {1 + cardHeight, rightLeftOffset, detailHeight, rightWidth};
    layout.detailContent = {2 + cardHeight, rightLeftOffset + 1, detailHeight - 2, rightWidth - 2};
    return layout;
}

} // namespace retrowave
