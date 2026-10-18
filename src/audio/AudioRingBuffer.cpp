// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#include "audio/AudioRingBuffer.h"

#include <algorithm>
#include <bit>

namespace retrowave {

AudioRingBuffer::AudioRingBuffer() = default;

AudioRingBuffer::AudioRingBuffer(std::size_t capacityFrames, int channels) {
    reset(capacityFrames, channels);
}

void AudioRingBuffer::reset(std::size_t capacityFrames, int channels) {
    channels_ = std::max(1, channels);
    capacityFrames_ = std::bit_ceil(capacityFrames);
    mask_ = capacityFrames_ - 1;
    buffer_.assign(capacityFrames_ * static_cast<std::size_t>(channels_), 0);
    readPos_.store(0, std::memory_order_seq_cst);
    writePos_.store(0, std::memory_order_seq_cst);
    eof_.store(false, std::memory_order_seq_cst);
}

void AudioRingBuffer::flush() {
    const std::size_t currentWrite = writePos_.load(std::memory_order_acquire);
    readPos_.store(currentWrite, std::memory_order_release);
    eof_.store(false, std::memory_order_release);
}

std::size_t AudioRingBuffer::push(const std::int16_t* source, std::size_t frames) {
    if (source == nullptr || frames == 0 || buffer_.empty()) {
        return 0;
    }

    const std::size_t capacity = capacityFrames_;
    const std::size_t currentWrite = writePos_.load(std::memory_order_relaxed);
    const std::size_t currentRead = readPos_.load(std::memory_order_acquire);

    const std::size_t diff = currentWrite - currentRead;
    const std::size_t usedSpace = diff <= capacity ? diff : 0;
    const std::size_t availableSpace = capacity - usedSpace;
    const std::size_t framesToWrite = std::min(frames, availableSpace);

    if (framesToWrite == 0) {
        return 0;
    }

    const std::size_t writeIdx = currentWrite & mask_;
    const std::size_t numChannels = static_cast<std::size_t>(channels_);
    const std::size_t framesUntilWrap = capacity - writeIdx;
    
    if (framesToWrite <= framesUntilWrap) {
        std::copy_n(source, framesToWrite * numChannels, buffer_.data() + writeIdx * numChannels);
    } else {
        std::copy_n(source, framesUntilWrap * numChannels, buffer_.data() + writeIdx * numChannels);
        std::copy_n(source + framesUntilWrap * numChannels, (framesToWrite - framesUntilWrap) * numChannels, buffer_.data());
    }

    writePos_.store(currentWrite + framesToWrite, std::memory_order_release);
    return framesToWrite;
}

std::size_t AudioRingBuffer::pop(std::int16_t* destination, std::size_t frames) {
    if (destination == nullptr || frames == 0 || buffer_.empty()) {
        return 0;
    }

    const std::size_t capacity = capacityFrames_;
    const std::size_t currentRead = readPos_.load(std::memory_order_relaxed);
    const std::size_t currentWrite = writePos_.load(std::memory_order_acquire);

    const std::size_t diff = currentWrite - currentRead;
    const std::size_t availableData = diff <= capacity ? diff : 0;
    const std::size_t framesToRead = std::min(frames, availableData);

    if (framesToRead == 0) {
        return 0;
    }

    const std::size_t readIdx = currentRead & mask_;
    const std::size_t numChannels = static_cast<std::size_t>(channels_);
    const std::size_t framesUntilWrap = capacity - readIdx;

    if (framesToRead <= framesUntilWrap) {
        std::copy_n(buffer_.data() + readIdx * numChannels, framesToRead * numChannels, destination);
    } else {
        std::copy_n(buffer_.data() + readIdx * numChannels, framesUntilWrap * numChannels, destination);
        std::copy_n(buffer_.data(), (framesToRead - framesUntilWrap) * numChannels, destination + framesUntilWrap * numChannels);
    }

    readPos_.store(currentRead + framesToRead, std::memory_order_release);
    return framesToRead;
}

std::size_t AudioRingBuffer::availableFrames() const {
    const std::size_t currentRead = readPos_.load(std::memory_order_relaxed);
    const std::size_t currentWrite = writePos_.load(std::memory_order_acquire);
    const std::size_t diff = currentWrite - currentRead;
    return diff <= capacityFrames_ ? diff : 0;
}

std::size_t AudioRingBuffer::freeFrames() const {
    if (capacityFrames_ == 0) return 0;
    const std::size_t currentRead = readPos_.load(std::memory_order_acquire);
    const std::size_t currentWrite = writePos_.load(std::memory_order_relaxed);
    const std::size_t diff = currentWrite - currentRead;
    const std::size_t usedSpace = diff <= capacityFrames_ ? diff : 0;
    return capacityFrames_ - usedSpace;
}

int AudioRingBuffer::channels() const {
    return channels_;
}

void AudioRingBuffer::markEof() {
    eof_.store(true, std::memory_order_release);
}

void AudioRingBuffer::clearEof() {
    eof_.store(false, std::memory_order_release);
}

bool AudioRingBuffer::eof() const {
    return eof_.load(std::memory_order_acquire);
}

}  // namespace retrowave
