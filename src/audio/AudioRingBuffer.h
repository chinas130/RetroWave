// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace retrowave {

class AudioRingBuffer {
  public:
    AudioRingBuffer();
    AudioRingBuffer(std::size_t capacityFrames, int channels);

    void reset(std::size_t capacityFrames, int channels);
    void flush();

    [[nodiscard]] std::size_t push(const std::int16_t* source, std::size_t frames);
    [[nodiscard]] std::size_t pop(std::int16_t* destination, std::size_t frames);

    [[nodiscard]] std::size_t availableFrames() const;
    [[nodiscard]] std::size_t freeFrames() const;
    [[nodiscard]] int channels() const;

    void markEof();
    void clearEof();
    [[nodiscard]] bool eof() const;

  private:
    std::vector<std::int16_t> buffer_;
    std::size_t capacityFrames_ = 0;
    std::size_t mask_ = 0;
    int channels_ = 2;

    alignas(64) std::atomic<std::size_t> readPos_{0};
    alignas(64) std::atomic<std::size_t> writePos_{0};
    alignas(64) std::atomic<bool> eof_{false};
};

}  // namespace retrowave
