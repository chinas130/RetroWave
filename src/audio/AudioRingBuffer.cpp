#include "audio/AudioRingBuffer.h"

#include <algorithm>

namespace retrowave {

AudioRingBuffer::AudioRingBuffer() = default;

AudioRingBuffer::AudioRingBuffer(std::size_t capacityFrames, int channels) {
    reset(capacityFrames, channels);
}

void AudioRingBuffer::reset(std::size_t capacityFrames, int channels) {
    std::lock_guard lock(mutex_);
    channels_ = std::max(1, channels);
    buffer_.assign(capacityFrames * static_cast<std::size_t>(channels_), 0);
    readFrame_ = 0;
    writeFrame_ = 0;
    sizeFrames_ = 0;
    eof_ = false;
}

void AudioRingBuffer::clear() {
    std::lock_guard lock(mutex_);
    readFrame_ = 0;
    writeFrame_ = 0;
    sizeFrames_ = 0;
    eof_ = false;
}

std::size_t AudioRingBuffer::push(const std::int16_t* source, std::size_t frames) {
    std::lock_guard lock(mutex_);
    if (source == nullptr || frames == 0 || buffer_.empty()) {
        return 0;
    }

    const auto framesToWrite = std::min(frames, capacityFramesLocked() - sizeFrames_);
    if (framesToWrite == 0) {
        return 0;
    }

    for (std::size_t frame = 0; frame < framesToWrite; ++frame) {
        const auto destinationFrame = (writeFrame_ + frame) % capacityFramesLocked();
        for (int channel = 0; channel < channels_; ++channel) {
            buffer_[destinationFrame * static_cast<std::size_t>(channels_) + static_cast<std::size_t>(channel)] =
                source[frame * static_cast<std::size_t>(channels_) + static_cast<std::size_t>(channel)];
        }
    }

    writeFrame_ = (writeFrame_ + framesToWrite) % capacityFramesLocked();
    sizeFrames_ += framesToWrite;
    return framesToWrite;
}

std::size_t AudioRingBuffer::pop(std::int16_t* destination, std::size_t frames) {
    std::lock_guard lock(mutex_);
    if (destination == nullptr || frames == 0 || buffer_.empty()) {
        return 0;
    }

    const auto framesToRead = std::min(frames, sizeFrames_);
    if (framesToRead == 0) {
        return 0;
    }

    for (std::size_t frame = 0; frame < framesToRead; ++frame) {
        const auto sourceFrame = (readFrame_ + frame) % capacityFramesLocked();
        for (int channel = 0; channel < channels_; ++channel) {
            destination[frame * static_cast<std::size_t>(channels_) + static_cast<std::size_t>(channel)] =
                buffer_[sourceFrame * static_cast<std::size_t>(channels_) + static_cast<std::size_t>(channel)];
        }
    }

    readFrame_ = (readFrame_ + framesToRead) % capacityFramesLocked();
    sizeFrames_ -= framesToRead;
    return framesToRead;
}

std::size_t AudioRingBuffer::availableFrames() const {
    std::lock_guard lock(mutex_);
    return sizeFrames_;
}

std::size_t AudioRingBuffer::freeFrames() const {
    std::lock_guard lock(mutex_);
    return capacityFramesLocked() - sizeFrames_;
}

int AudioRingBuffer::channels() const {
    std::lock_guard lock(mutex_);
    return channels_;
}

void AudioRingBuffer::markEof() {
    std::lock_guard lock(mutex_);
    eof_ = true;
}

void AudioRingBuffer::clearEof() {
    std::lock_guard lock(mutex_);
    eof_ = false;
}

bool AudioRingBuffer::eof() const {
    std::lock_guard lock(mutex_);
    return eof_;
}

std::size_t AudioRingBuffer::capacityFramesLocked() const {
    if (channels_ <= 0) {
        return 0;
    }
    return buffer_.size() / static_cast<std::size_t>(channels_);
}

}  // namespace retrowave
