#include "audio/AudioStreamDecoder.h"

#include <algorithm>
#include <stdexcept>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libswresample/swresample.h>
}

namespace retrowave {
namespace {

constexpr int kOutputSampleRate = 44100;
constexpr int kOutputChannels = 2;

std::string ffmpegError(int errorCode) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(errorCode, buffer, sizeof(buffer));
    return std::string(buffer);
}

}  // namespace

AudioStreamDecoder::AudioStreamDecoder() = default;

AudioStreamDecoder::~AudioStreamDecoder() {
    close();
}

void AudioStreamDecoder::open(const std::filesystem::path& path) {
    close();

    const std::string pathString = path.string();
    if (avformat_open_input(&formatContext_, pathString.c_str(), nullptr, nullptr) < 0) {
        throw std::runtime_error("Cannot open stream for " + path.filename().string());
    }

    const int infoResult = avformat_find_stream_info(formatContext_, nullptr);
    if (infoResult < 0) {
        throw std::runtime_error("Cannot inspect stream info: " + ffmpegError(infoResult));
    }

    audioStreamIndex_ = av_find_best_stream(formatContext_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audioStreamIndex_ < 0) {
        throw std::runtime_error("No audio stream found: " + ffmpegError(audioStreamIndex_));
    }

    AVStream* stream = formatContext_->streams[audioStreamIndex_];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) {
        throw std::runtime_error("Unsupported codec for " + path.filename().string());
    }

    codecContext_ = avcodec_alloc_context3(codec);
    if (codecContext_ == nullptr) {
        throw std::runtime_error("Cannot allocate streaming decoder context.");
    }

    const int parameterResult = avcodec_parameters_to_context(codecContext_, stream->codecpar);
    if (parameterResult < 0) {
        throw std::runtime_error("Cannot copy codec parameters: " + ffmpegError(parameterResult));
    }

    const int openResult = avcodec_open2(codecContext_, codec, nullptr);
    if (openResult < 0) {
        throw std::runtime_error("Cannot open streaming decoder: " + ffmpegError(openResult));
    }

    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, kOutputChannels);

    AVChannelLayout inLayout = codecContext_->ch_layout;
    if (inLayout.nb_channels == 0) {
        av_channel_layout_default(&inLayout, 2);
    }

    const int swrSetupResult = swr_alloc_set_opts2(
        &resampler_,
        &outLayout,
        AV_SAMPLE_FMT_S16,
        kOutputSampleRate,
        &inLayout,
        codecContext_->sample_fmt,
        codecContext_->sample_rate,
        0,
        nullptr);

    if (swrSetupResult < 0 || resampler_ == nullptr) {
        av_channel_layout_uninit(&outLayout);
        throw std::runtime_error("Cannot configure streaming resampler: " + ffmpegError(swrSetupResult));
    }

    const int swrInitResult = swr_init(resampler_);
    av_channel_layout_uninit(&outLayout);
    if (swrInitResult < 0) {
        throw std::runtime_error("Cannot initialize streaming resampler: " + ffmpegError(swrInitResult));
    }

    packet_ = av_packet_alloc();
    frame_ = av_frame_alloc();
    if (packet_ == nullptr || frame_ == nullptr) {
        throw std::runtime_error("Cannot allocate streaming packet/frame.");
    }

    eof_ = false;
    readEof_ = false;
    flushSent_ = false;
    clearPendingSamples();
}

void AudioStreamDecoder::close() {
    clearPendingSamples();

    if (frame_ != nullptr) {
        av_frame_free(&frame_);
    }
    if (packet_ != nullptr) {
        av_packet_free(&packet_);
    }
    if (resampler_ != nullptr) {
        swr_free(&resampler_);
    }
    if (codecContext_ != nullptr) {
        avcodec_free_context(&codecContext_);
    }
    if (formatContext_ != nullptr) {
        avformat_close_input(&formatContext_);
    }

    audioStreamIndex_ = -1;
    eof_ = false;
    readEof_ = false;
    flushSent_ = false;
}

std::size_t AudioStreamDecoder::readFrames(std::int16_t* destination, std::size_t maxFrames) {
    if (destination == nullptr || maxFrames == 0) {
        return 0;
    }

    std::size_t writtenFrames = 0;
    while (writtenFrames < maxFrames) {
        const auto pendingFrames =
            (pendingSamples_.size() - pendingOffset_) / static_cast<std::size_t>(kOutputChannels);
        if (pendingFrames > 0) {
            const auto framesToCopy = std::min(maxFrames - writtenFrames, pendingFrames);
            const auto samplesToCopy = framesToCopy * static_cast<std::size_t>(kOutputChannels);
            std::copy_n(
                pendingSamples_.data() + static_cast<std::ptrdiff_t>(pendingOffset_),
                static_cast<std::ptrdiff_t>(samplesToCopy),
                destination + static_cast<std::ptrdiff_t>(writtenFrames * static_cast<std::size_t>(kOutputChannels)));
            pendingOffset_ += samplesToCopy;
            writtenFrames += framesToCopy;

            if (pendingOffset_ >= pendingSamples_.size()) {
                clearPendingSamples();
            }
            continue;
        }

        if (!decodeNextChunk()) {
            break;
        }
    }

    return writtenFrames;
}

bool AudioStreamDecoder::eof() const noexcept {
    return eof_;
}

int AudioStreamDecoder::sampleRate() const noexcept {
    return kOutputSampleRate;
}

int AudioStreamDecoder::channels() const noexcept {
    return kOutputChannels;
}

bool AudioStreamDecoder::decodeNextChunk() {
    clearPendingSamples();

    while (true) {
        const int receiveResult = avcodec_receive_frame(codecContext_, frame_);
        if (receiveResult == 0) {
            const int dstSamples = av_rescale_rnd(
                swr_get_delay(resampler_, codecContext_->sample_rate) + frame_->nb_samples,
                kOutputSampleRate,
                codecContext_->sample_rate,
                AV_ROUND_UP);

            pendingSamples_.assign(static_cast<std::size_t>(dstSamples) * kOutputChannels, 0);
            uint8_t* outPlanes[] = {reinterpret_cast<uint8_t*>(pendingSamples_.data())};
            const uint8_t** inPlanes = const_cast<const uint8_t**>(frame_->extended_data);

            const int written = swr_convert(resampler_, outPlanes, dstSamples, inPlanes, frame_->nb_samples);
            av_frame_unref(frame_);
            if (written < 0) {
                throw std::runtime_error("Streaming resample failed: " + ffmpegError(written));
            }

            pendingSamples_.resize(static_cast<std::size_t>(written) * kOutputChannels);
            pendingOffset_ = 0;
            return !pendingSamples_.empty();
        }

        if (receiveResult == AVERROR_EOF) {
            eof_ = true;
            return false;
        }

        if (receiveResult != AVERROR(EAGAIN)) {
            throw std::runtime_error("Streaming decode receive failed: " + ffmpegError(receiveResult));
        }

        if (readEof_) {
            if (!flushSent_) {
                const int flushResult = avcodec_send_packet(codecContext_, nullptr);
                if (flushResult < 0 && flushResult != AVERROR_EOF) {
                    throw std::runtime_error("Streaming flush failed: " + ffmpegError(flushResult));
                }
                flushSent_ = true;
                continue;
            }

            eof_ = true;
            return false;
        }

        while (true) {
            const int readResult = av_read_frame(formatContext_, packet_);
            if (readResult == AVERROR_EOF) {
                readEof_ = true;
                break;
            }
            if (readResult < 0) {
                throw std::runtime_error("Streaming packet read failed: " + ffmpegError(readResult));
            }

            if (packet_->stream_index != audioStreamIndex_) {
                av_packet_unref(packet_);
                continue;
            }

            const int sendResult = avcodec_send_packet(codecContext_, packet_);
            av_packet_unref(packet_);
            if (sendResult == AVERROR(EAGAIN)) {
                break;
            }
            if (sendResult < 0) {
                throw std::runtime_error("Streaming decoder send failed: " + ffmpegError(sendResult));
            }
            break;
        }
    }
}

void AudioStreamDecoder::clearPendingSamples() {
    pendingSamples_.clear();
    pendingOffset_ = 0;
}

}  // namespace retrowave
