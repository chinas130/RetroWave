// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#pragma once

#include "audio/ExternalProcess.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

extern "C" {
#include <libavutil/channel_layout.h>
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
    void openUrl(const std::string& url);
    bool seekSeconds(double seconds);
    void requestStop();
    void close();

    [[nodiscard]] std::size_t readFrames(std::int16_t* destination, std::size_t maxFrames);
    [[nodiscard]] bool eof() const noexcept;
    [[nodiscard]] int sampleRate() const noexcept;
    [[nodiscard]] int channels() const noexcept;
    [[nodiscard]] double durationSeconds() const noexcept;
    [[nodiscard]] bool live() const noexcept;
    [[nodiscard]] AVFormatContext* formatContext() const noexcept;
    [[nodiscard]] int audioStreamIndex() const noexcept;

  private:
    void openInput(const std::string& input, const std::string& label);
    void openConvertedInput(const std::string& input, const std::string& label);
    bool decodeNextChunk();
    void clearPendingSamples();
    [[nodiscard]] std::size_t readConvertedFrames(std::int16_t* destination, std::size_t maxFrames);

    ExternalProcess converterProcess_;
    AVFormatContext* formatContext_ = nullptr;
    AVCodecContext* codecContext_ = nullptr;
    AVPacket* packet_ = nullptr;
    AVFrame* frame_ = nullptr;
    SwrContext* resampler_ = nullptr;
    int audioStreamIndex_ = -1;
    bool eof_ = false;
    bool live_ = false;
    bool readEof_ = false;
    bool flushSent_ = false;
    double durationSeconds_ = 0.0;
    std::string originalInput_;
    std::string originalLabel_;
    bool isConverted_ = false;
    std::vector<std::int16_t> pendingSamples_;
    std::size_t pendingOffset_ = 0;

    int currentSampleRate_ = 0;
    int currentSampleFormat_ = -1;
    int currentChannels_ = 0;
    AVChannelLayout currentLayout_{};
};

}  // namespace retrowave