#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

extern "C" {
struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct AVPacket;
struct SwrContext;
}

namespace retrowave {

class AudioStreamDecoder {
  public:
    AudioStreamDecoder();
    ~AudioStreamDecoder();

    AudioStreamDecoder(const AudioStreamDecoder&) = delete;
    AudioStreamDecoder& operator=(const AudioStreamDecoder&) = delete;

    void open(const std::filesystem::path& path);
    void close();

    [[nodiscard]] std::size_t readFrames(std::int16_t* destination, std::size_t maxFrames);
    [[nodiscard]] bool eof() const noexcept;
    [[nodiscard]] int sampleRate() const noexcept;
    [[nodiscard]] int channels() const noexcept;

  private:
    bool decodeNextChunk();
    void clearPendingSamples();

    AVFormatContext* formatContext_ = nullptr;
    AVCodecContext* codecContext_ = nullptr;
    AVPacket* packet_ = nullptr;
    AVFrame* frame_ = nullptr;
    SwrContext* resampler_ = nullptr;
    int audioStreamIndex_ = -1;
    bool eof_ = false;
    bool readEof_ = false;
    bool flushSent_ = false;
    std::vector<std::int16_t> pendingSamples_;
    std::size_t pendingOffset_ = 0;
};

}  // namespace retrowave
