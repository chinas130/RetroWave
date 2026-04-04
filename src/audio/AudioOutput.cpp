#include "audio/AudioOutput.h"

#include <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace retrowave {
namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kChannelCount = 2;
constexpr std::size_t kFramesPerBuffer = 2048;
constexpr std::size_t kBufferCount = 3;

void checkStatus(OSStatus status, const char* message) {
    if (status != noErr) {
        throw std::runtime_error(std::string(message) + " (OSStatus=" + std::to_string(status) + ")");
    }
}

}  // namespace

AudioOutput::AudioOutput(RenderCallback render) : render_(std::move(render)) {
    AudioStreamBasicDescription description{};
    description.mSampleRate = kSampleRate;
    description.mFormatID = kAudioFormatLinearPCM;
    description.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    description.mFramesPerPacket = 1;
    description.mChannelsPerFrame = kChannelCount;
    description.mBitsPerChannel = 16;
    description.mBytesPerFrame = static_cast<std::uint32_t>(sizeof(std::int16_t) * kChannelCount);
    description.mBytesPerPacket = description.mBytesPerFrame;

    checkStatus(
        AudioQueueNewOutput(&description, &AudioOutput::handleBuffer, this, nullptr, nullptr, 0, &queue_),
        "Unable to create audio queue");

    buffers_.reserve(kBufferCount);
    for (std::size_t index = 0; index < kBufferCount; ++index) {
        AudioQueueBufferRef buffer = nullptr;
        checkStatus(
            AudioQueueAllocateBuffer(queue_, static_cast<std::uint32_t>(kFramesPerBuffer * description.mBytesPerFrame), &buffer),
            "Unable to allocate audio buffer");
        buffers_.push_back(buffer);
    }
}

AudioOutput::~AudioOutput() {
    stop();
}

void AudioOutput::start() {
    if (started_) {
        return;
    }

    for (auto* buffer : buffers_) {
        fillAndEnqueue(buffer);
    }

    checkStatus(AudioQueueStart(queue_, nullptr), "Unable to start audio queue");
    started_ = true;
}

void AudioOutput::stop() {
    if (queue_ == nullptr) {
        return;
    }

    AudioQueueStop(queue_, true);
    AudioQueueDispose(queue_, true);
    queue_ = nullptr;
    started_ = false;
}

void AudioOutput::handleBuffer(void* userData, AudioQueueRef, AudioQueueBufferRef buffer) {
    auto* output = static_cast<AudioOutput*>(userData);
    if (output == nullptr || output->queue_ == nullptr) {
        return;
    }

    output->fillAndEnqueue(buffer);
}

void AudioOutput::fillAndEnqueue(AudioQueueBufferRef buffer) {
    auto* samples = static_cast<std::int16_t*>(buffer->mAudioData);
    const std::size_t framesWritten = render_(samples, kFramesPerBuffer);
    const auto bytesWritten = static_cast<std::uint32_t>(
        std::max<std::size_t>(framesWritten, kFramesPerBuffer) * sizeof(std::int16_t) * kChannelCount);

    if (framesWritten < kFramesPerBuffer) {
        const auto offset = framesWritten * kChannelCount;
        const auto remaining = (kFramesPerBuffer - framesWritten) * kChannelCount;
        std::memset(samples + offset, 0, remaining * sizeof(std::int16_t));
    }

    buffer->mAudioDataByteSize = bytesWritten;
    AudioQueueEnqueueBuffer(queue_, buffer, 0, nullptr);
}

}  // namespace retrowave
