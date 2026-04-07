#include "audio/AudioOutput.h"

#include <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace retrowave {
namespace {

constexpr int kChannelCount = 2;

void checkStatus(OSStatus status, const char* message) {
    if (status != noErr) {
        throw std::runtime_error(std::string(message) + " (OSStatus=" + std::to_string(status) + ")");
    }
}

}  // namespace

struct AudioOutput::Impl {
    explicit Impl(RenderCallback render) : render_(std::move(render)) {
        AudioStreamBasicDescription description{};
        description.mSampleRate = AudioOutput::sampleRate();
        description.mFormatID = kAudioFormatLinearPCM;
        description.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
        description.mFramesPerPacket = 1;
        description.mChannelsPerFrame = kChannelCount;
        description.mBitsPerChannel = 16;
        description.mBytesPerFrame = static_cast<std::uint32_t>(sizeof(std::int16_t) * kChannelCount);
        description.mBytesPerPacket = description.mBytesPerFrame;

        checkStatus(
            AudioQueueNewOutput(&description, &Impl::handleBuffer, this, nullptr, nullptr, 0, &queue_),
            "Unable to create audio queue");

        buffers_.reserve(AudioOutput::bufferCount());
        for (std::size_t index = 0; index < AudioOutput::bufferCount(); ++index) {
            AudioQueueBufferRef buffer = nullptr;
            checkStatus(
                AudioQueueAllocateBuffer(
                    queue_,
                    static_cast<std::uint32_t>(AudioOutput::framesPerBuffer() * description.mBytesPerFrame),
                    &buffer),
                "Unable to allocate audio buffer");
            buffers_.push_back(buffer);
        }
    }

    ~Impl() {
        stop();
    }

    void start() {
        if (started_) {
            return;
        }

        for (auto* buffer : buffers_) {
            fillAndEnqueue(buffer);
        }

        checkStatus(AudioQueueStart(queue_, nullptr), "Unable to start audio queue");
        started_ = true;
    }

    void stop() {
        if (queue_ == nullptr) {
            return;
        }

        AudioQueueStop(queue_, true);
        AudioQueueDispose(queue_, true);
        queue_ = nullptr;
        started_ = false;
    }

    static void handleBuffer(void* userData, AudioQueueRef, AudioQueueBufferRef buffer) {
        auto* impl = static_cast<Impl*>(userData);
        if (impl == nullptr || impl->queue_ == nullptr) {
            return;
        }

        impl->fillAndEnqueue(buffer);
    }

    void fillAndEnqueue(AudioQueueBufferRef buffer) {
        auto* samples = static_cast<std::int16_t*>(buffer->mAudioData);
        const std::size_t framesWritten = render_(samples, AudioOutput::framesPerBuffer());
        const auto bytesWritten = static_cast<std::uint32_t>(
            std::max<std::size_t>(framesWritten, AudioOutput::framesPerBuffer()) * sizeof(std::int16_t) * kChannelCount);

        if (framesWritten < AudioOutput::framesPerBuffer()) {
            const auto offset = framesWritten * kChannelCount;
            const auto remaining = (AudioOutput::framesPerBuffer() - framesWritten) * kChannelCount;
            std::memset(samples + offset, 0, remaining * sizeof(std::int16_t));
        }

        buffer->mAudioDataByteSize = bytesWritten;
        AudioQueueEnqueueBuffer(queue_, buffer, 0, nullptr);
    }

    RenderCallback render_;
    AudioQueueRef queue_ = nullptr;
    std::vector<AudioQueueBufferRef> buffers_;
    bool started_ = false;
};

AudioOutput::AudioOutput(RenderCallback render)
    : impl_(std::make_unique<Impl>(std::move(render))) {}

AudioOutput::~AudioOutput() = default;

void AudioOutput::start() {
    impl_->start();
}

void AudioOutput::stop() {
    impl_->stop();
}

}  // namespace retrowave
