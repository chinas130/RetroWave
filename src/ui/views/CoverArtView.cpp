// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#include "ui/views/CoverArtView.h"
#include <algorithm>
#include <cmath>

namespace retrowave {

std::vector<std::string> CoverArtView::renderPlaceholderArt(int width, int height) {
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

std::vector<float> CoverArtView::sampleAlbumArt(
    const std::shared_ptr<const AlbumArt>& art,
    int targetWidth,
    int targetHeight,
    float& minGray,
    float& maxGray) {
    std::vector<float> sampled(static_cast<std::size_t>(targetWidth * targetHeight), 0.0F);
    minGray = 255.0F;
    maxGray = 0.0F;

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

    return sampled;
}

std::vector<std::string> CoverArtView::renderCoverArt(
    const std::shared_ptr<const AlbumArt>& art,
    int maxWidth,
    int maxHeight,
    CoverArtMode mode) {
    if (!art || art->empty() || maxWidth <= 0 || maxHeight <= 0) {
        return renderPlaceholderArt(maxWidth, maxHeight);
    }

    constexpr double kCharAspect = 0.5;

    const double imageAspect = static_cast<double>(art->width) / static_cast<double>(std::max(1, art->height));
    int targetWidth = std::max(1, std::min(maxWidth, static_cast<int>(std::round(maxHeight * imageAspect / kCharAspect))));
    int targetHeight = std::max(1, std::min(maxHeight, static_cast<int>(std::round(targetWidth * kCharAspect / imageAspect))));

    if (mode == CoverArtMode::HalfBlock) {
        targetWidth = std::max(1, std::min(maxWidth, static_cast<int>(std::round(maxHeight * imageAspect / 1.0))));
        targetHeight = std::max(1, std::min(maxHeight * 2, static_cast<int>(std::round(targetWidth / imageAspect))));
    }

    if (targetHeight > maxHeight) {
        targetHeight = maxHeight;
        targetWidth = std::max(1, std::min(maxWidth, static_cast<int>(std::round(targetHeight * imageAspect / kCharAspect))));
    }

    if (mode == CoverArtMode::HalfBlock && targetHeight > maxHeight * 2) {
        targetHeight = maxHeight * 2;
        targetWidth = std::max(1, std::min(maxWidth, static_cast<int>(std::round((targetHeight / 2.0) * imageAspect))));
    }

    float minGray = 255.0F;
    float maxGray = 0.0F;
    const auto sampled = sampleAlbumArt(art, targetWidth, targetHeight, minGray, maxGray);
    const float span = std::max(1.0F, maxGray - minGray);
    std::vector<std::string> lines;
    if (mode == CoverArtMode::Ascii) {
        static const std::string kRamp = "  ..,,::--==++**##%%@@";
        lines.reserve(static_cast<std::size_t>(targetHeight));
        for (int row = 0; row < targetHeight; ++row) {
            std::string line;
            line.reserve(static_cast<std::size_t>(targetWidth));

            for (int column = 0; column < targetWidth; ++column) {
                const float gray = sampled[static_cast<std::size_t>(row * targetWidth + column)];
                const float normalized = std::pow(std::clamp((gray - minGray) / span, 0.0F, 1.0F), 1.12F);
                const std::size_t rampIndex = static_cast<std::size_t>(
                    std::round((1.0F - normalized) * static_cast<float>(kRamp.size() - 1)));
                line.push_back(kRamp[rampIndex]);
            }

            lines.push_back(std::move(line));
        }
        return lines;
    }

    if (mode == CoverArtMode::BlockShading) {
        static const std::vector<std::string> kRamp = {" ", "░", "▒", "▓", "█"};
        lines.reserve(static_cast<std::size_t>(targetHeight));
        for (int row = 0; row < targetHeight; ++row) {
            std::string line;
            for (int column = 0; column < targetWidth; ++column) {
                const float gray = sampled[static_cast<std::size_t>(row * targetWidth + column)];
                const float normalized = std::pow(std::clamp((gray - minGray) / span, 0.0F, 1.0F), 1.05F);
                const std::size_t rampIndex = static_cast<std::size_t>(
                    std::round((1.0F - normalized) * static_cast<float>(kRamp.size() - 1)));
                line += kRamp[rampIndex];
            }
            lines.push_back(std::move(line));
        }
        return lines;
    }

    lines.reserve(static_cast<std::size_t>((targetHeight + 1) / 2));
    for (int row = 0; row < targetHeight; row += 2) {
        std::string line;
        for (int column = 0; column < targetWidth; ++column) {
            const float topGray = sampled[static_cast<std::size_t>(row * targetWidth + column)];
            const float bottomGray = sampled[static_cast<std::size_t>(std::min(row + 1, targetHeight - 1) * targetWidth + column)];
            const float topDark = 1.0F - std::clamp((topGray - minGray) / span, 0.0F, 1.0F);
            const float bottomDark = 1.0F - std::clamp((bottomGray - minGray) / span, 0.0F, 1.0F);

            if (topDark < 0.18F && bottomDark < 0.18F) {
                line += " ";
            } else if (topDark > 0.72F && bottomDark > 0.72F) {
                line += "█";
            } else if (topDark >= bottomDark) {
                line += topDark > 0.38F ? "▀" : " ";
            } else {
                line += bottomDark > 0.38F ? "▄" : " ";
            }
        }
        lines.push_back(std::move(line));
    }

    return lines;
}

} // namespace retrowave
