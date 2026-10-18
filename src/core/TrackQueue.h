// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#pragma once

#include "core/PlaybackMode.h"

#include <cstddef>
#include <mutex>
#include <optional>
#include <vector>

namespace retrowave {

class TrackQueue {
  public:
    TrackQueue() = default;

    TrackQueue(const TrackQueue&) = delete;
    TrackQueue& operator=(const TrackQueue&) = delete;

    void setRepeatMode(RepeatMode mode);
    [[nodiscard]] RepeatMode repeatMode() const noexcept;

    void setShuffleMode(ShuffleMode mode, std::size_t currentIndex, std::size_t playlistSize);
    [[nodiscard]] ShuffleMode shuffleMode() const noexcept;

    [[nodiscard]] std::optional<std::size_t> getNext(std::size_t currentIndex, std::size_t playlistSize);
    [[nodiscard]] std::size_t getPrevious(std::size_t currentIndex, std::size_t playlistSize);

    void onTrackSelected(std::size_t index, std::size_t playlistSize);
    void clearHistory();

  private:
    void trimShuffleHistory();
    void rebuildShuffleOrder(std::size_t anchorIndex, std::size_t playlistSize);
    [[nodiscard]] std::size_t findShufflePosition(std::size_t index) const;
    void syncShufflePosition(std::size_t index, std::size_t playlistSize);
    [[nodiscard]] std::size_t startNewShufflePass(std::size_t avoidIndex, std::size_t playlistSize);

    mutable std::mutex mutex_;
    RepeatMode repeatMode_{RepeatMode::Off};
    ShuffleMode shuffleMode_{ShuffleMode::Off};

    std::vector<std::size_t> shuffleOrder_;
    std::size_t shufflePosition_ = 0;
    std::vector<std::size_t> playbackHistory_;
    static constexpr std::size_t kMaxPlaybackHistory = 256;
};

}  // namespace retrowave
