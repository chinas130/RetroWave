#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace retrowave {

class AudioRingBuffer {
  public:
    AudioRingBuffer();
    AudioRingBuffer(std::size_t capacityFrames, int channels);

    void reset(std::size_t capacityFrames, int channels);
    void clear();

    [[nodiscard]] std::size_t push(const std::int16_t* source, std::size_t frames);
    [[nodiscard]] std::size_t pop(std::int16_t* destination, std::size_t frames);

    [[nodiscard]] std::size_t availableFrames() const;
    [[nodiscard]] std::size_t freeFrames() const;
    [[nodiscard]] int channels() const;

    void markEof();
    void clearEof();
    [[nodiscard]] bool eof() const;

  private:
    [[nodiscard]] std::size_t capacityFramesLocked() const;

    mutable std::mutex mutex_;
    std::vector<std::int16_t> buffer_;
    std::size_t readFrame_ = 0;
    std::size_t writeFrame_ = 0;
    std::size_t sizeFrames_ = 0;
    int channels_ = 2;
    bool eof_ = false;
};

}  // namespace retrowave
