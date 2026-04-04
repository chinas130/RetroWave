#pragma once

#include <AudioToolbox/AudioQueue.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace retrowave {

class AudioOutput {
  public:
    using RenderCallback = std::function<std::size_t(std::int16_t* destination, std::size_t frames)>;

    static constexpr double sampleRate() noexcept { return 44100.0; }
    static constexpr std::size_t framesPerBuffer() noexcept { return 2048; }
    static constexpr std::size_t bufferCount() noexcept { return 3; }
    static constexpr std::size_t queuedLatencyFrames() noexcept { return framesPerBuffer() * bufferCount(); }

    explicit AudioOutput(RenderCallback render);
    ~AudioOutput();

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    void start();
    void stop();

  private:
    static void handleBuffer(void* userData, AudioQueueRef queue, AudioQueueBufferRef buffer);
    void fillAndEnqueue(AudioQueueBufferRef buffer);

    RenderCallback render_;
    AudioQueueRef queue_ = nullptr;
    std::vector<AudioQueueBufferRef> buffers_;
    bool started_ = false;
};

}  // namespace retrowave
