// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#include "core/TrackQueue.h"

#include <numeric>
#include <random>

namespace retrowave {

void TrackQueue::setRepeatMode(RepeatMode mode) {
    std::lock_guard lock(mutex_);
    repeatMode_ = mode;
}

RepeatMode TrackQueue::repeatMode() const noexcept {
    std::lock_guard lock(mutex_);
    return repeatMode_;
}

void TrackQueue::setShuffleMode(ShuffleMode mode, std::size_t currentIndex, std::size_t playlistSize) {
    std::lock_guard lock(mutex_);
    shuffleMode_ = mode;
    if (mode == ShuffleMode::On) {
        syncShufflePosition(currentIndex, playlistSize);
        playbackHistory_.clear();
        playbackHistory_.shrink_to_fit();
    } else {
        shuffleOrder_.clear();
        playbackHistory_.clear();
    }
}

ShuffleMode TrackQueue::shuffleMode() const noexcept {
    std::lock_guard lock(mutex_);
    return shuffleMode_;
}

std::optional<std::size_t> TrackQueue::getNext(std::size_t currentIndex, std::size_t playlistSize) {
    if (playlistSize == 0) {
        return std::nullopt;
    }

    std::lock_guard lock(mutex_);

    if (shuffleMode_ == ShuffleMode::Off) {
        if (currentIndex + 1 < playlistSize) {
            return currentIndex + 1;
        }

        switch (repeatMode_) {
            case RepeatMode::One:
                return currentIndex;
            case RepeatMode::All:
                return 0;
            case RepeatMode::Off:
                return std::nullopt;
        }
    }

    if (shuffleOrder_.size() != playlistSize) {
        rebuildShuffleOrder(currentIndex, playlistSize);
    }

    auto recordHistory = [this, currentIndex]() {
        if (playbackHistory_.empty() || playbackHistory_.back() != currentIndex) {
            playbackHistory_.push_back(currentIndex);
            trimShuffleHistory();
        }
    };

    if (shufflePosition_ + 1 < shuffleOrder_.size()) {
        recordHistory();
        shufflePosition_++;
        return shuffleOrder_[shufflePosition_];
    }

    switch (repeatMode_) {
        case RepeatMode::One:
            return currentIndex;
        case RepeatMode::All:
            recordHistory();
            return startNewShufflePass(currentIndex, playlistSize);
        case RepeatMode::Off:
            return std::nullopt;
    }

    return std::nullopt;
}

std::size_t TrackQueue::getPrevious(std::size_t currentIndex, std::size_t playlistSize) {
    if (playlistSize == 0) {
        return 0;
    }

    std::lock_guard lock(mutex_);

    if (shuffleMode_ == ShuffleMode::Off) {
        if (currentIndex > 0) {
            return currentIndex - 1;
        }
        return repeatMode_ == RepeatMode::All ? playlistSize - 1 : 0;
    }

    if (shuffleOrder_.size() != playlistSize) {
        rebuildShuffleOrder(currentIndex, playlistSize);
    }

    if (!playbackHistory_.empty()) {
        const auto previousIndex = playbackHistory_.back();
        playbackHistory_.pop_back();
        shufflePosition_ = findShufflePosition(previousIndex);
        return previousIndex;
    }

    if (shufflePosition_ > 0) {
        shufflePosition_--;
        return shuffleOrder_[shufflePosition_];
    }

    if (repeatMode_ == RepeatMode::All && playlistSize > 1) {
        shufflePosition_ = playlistSize - 1;
        return shuffleOrder_[shufflePosition_];
    }

    return currentIndex;
}

void TrackQueue::onTrackSelected(std::size_t index, std::size_t playlistSize) {
    std::lock_guard lock(mutex_);
    playbackHistory_.clear();
    playbackHistory_.shrink_to_fit();
    if (shuffleMode_ == ShuffleMode::On) {
        syncShufflePosition(index, playlistSize);
    }
}

void TrackQueue::clearHistory() {
    std::lock_guard lock(mutex_);
    playbackHistory_.clear();
    playbackHistory_.shrink_to_fit();
}

void TrackQueue::trimShuffleHistory() {
    if (playbackHistory_.size() <= kMaxPlaybackHistory) {
        return;
    }
    const auto eraseCount = playbackHistory_.size() - kMaxPlaybackHistory;
    playbackHistory_.erase(playbackHistory_.begin(), playbackHistory_.begin() + static_cast<std::ptrdiff_t>(eraseCount));
}

void TrackQueue::rebuildShuffleOrder(std::size_t anchorIndex, std::size_t playlistSize) {
    shuffleOrder_.resize(playlistSize);
    if (playlistSize == 0) {
        shufflePosition_ = 0;
        return;
    }

    std::iota(shuffleOrder_.begin(), shuffleOrder_.end(), std::size_t{0});
    if (playlistSize > 1) {
        std::random_device rd;
        std::mt19937 gen(rd());
        for (std::size_t i = playlistSize - 1; i > 0; --i) {
            std::uniform_int_distribution<std::size_t> dis(0, i);
            std::swap(shuffleOrder_[i], shuffleOrder_[dis(gen)]);
        }
    }

    shufflePosition_ = findShufflePosition(anchorIndex);
}

std::size_t TrackQueue::findShufflePosition(std::size_t index) const {
    for (std::size_t position = 0; position < shuffleOrder_.size(); ++position) {
        if (shuffleOrder_[position] == index) {
            return position;
        }
    }
    return 0;
}

void TrackQueue::syncShufflePosition(std::size_t index, std::size_t playlistSize) {
    if (shuffleOrder_.size() != playlistSize) {
        rebuildShuffleOrder(index, playlistSize);
        return;
    }
    shufflePosition_ = findShufflePosition(index);
}

std::size_t TrackQueue::startNewShufflePass(std::size_t avoidIndex, std::size_t playlistSize) {
    rebuildShuffleOrder(avoidIndex, playlistSize);
    if (shuffleOrder_.size() <= 1) {
        shufflePosition_ = 0;
        return shuffleOrder_.empty() ? 0 : shuffleOrder_.front();
    }

    shufflePosition_ = 0;
    if (shuffleOrder_[0] != avoidIndex) {
        return shuffleOrder_[0];
    }

    shufflePosition_ = 1;
    return shuffleOrder_[1];
}

}  // namespace retrowave
