#include "audio/AudioStreamDecoder.h"

#include "audio/YtDlpResolver.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
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

bool looksLikeHlsUrl(const std::string& input) {
    std::string lower = input;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char symbol) {
        return static_cast<char>(std::tolower(symbol));
    });

    const auto query = input.find_first_of("?#");
    const auto path = input.substr(0, query);
    return path.ends_with(".m3u8") ||
        path.ends_with(".m3u") ||
        path.ends_with(".M3U8") ||
        path.ends_with(".M3U") ||
        lower.find("hls_playlist") != std::string::npos ||
        lower.find("manifest.googlevideo.com") != std::string::npos;
}

bool looksLikeHlsPlaylistFile(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path)) {
        return false;
    }

    std::ifstream input(path);
    if (!input) {
        return false;
    }

    std::string line;
    for (int inspectedLines = 0; inspectedLines < 64 && std::getline(input, line); ++inspectedLines) {
        line.erase(line.begin(), std::find_if(line.begin(), line.end(), [](unsigned char symbol) {
            return !std::isspace(symbol);
        }));
        std::transform(line.begin(), line.end(), line.begin(), [](unsigned char symbol) {
            return static_cast<char>(std::tolower(symbol));
        });
        if (line.starts_with("#ext-x-")) {
            return true;
        }
    }
    return false;
}

double inferDurationSeconds(const AVFormatContext* formatContext, const AVStream* stream) {
    if (formatContext != nullptr && formatContext->duration > 0) {
        return static_cast<double>(formatContext->duration) / static_cast<double>(AV_TIME_BASE);
    }
    if (stream != nullptr && stream->duration > 0) {
        return static_cast<double>(stream->duration) * av_q2d(stream->time_base);
    }
    return 0.0;
}

}  // namespace

AudioStreamDecoder::AudioStreamDecoder() = default;

AudioStreamDecoder::~AudioStreamDecoder() {
    close();
}

void AudioStreamDecoder::open(const std::filesystem::path& path) {
    if (looksLikeHlsUrl(path.string()) || looksLikeHlsPlaylistFile(path)) {
        openConvertedInput(path.string(), path.filename().string());
        return;
    }

    openInput(path.string(), path.filename().string());
}

void AudioStreamDecoder::openUrl(const std::string& url) {
    if (looksLikeHlsUrl(url) || YtDlpResolver::isYouTubeUrl(url)) {
        openConvertedInput(url, url);
        return;
    }

    openInput(url, url);
}

void AudioStreamDecoder::openInput(const std::string& input, const std::string& label) {
    close();

    AVDictionary* options = nullptr;
    av_dict_set(&options, "timeout", "7000000", 0);
    av_dict_set(&options, "user_agent", "RetroWave/0.1", 0);
    av_dict_set(&options, "reconnect", "1", 0);
    av_dict_set(&options, "reconnect_streamed", "1", 0);
    av_dict_set(&options, "reconnect_delay_max", "5", 0);
    av_dict_set(&options, "allowed_extensions", "ALL", 0);
    av_dict_set(&options, "protocol_whitelist", "file,http,https,tcp,tls,crypto,pipe,data", 0);

    const int openResult = avformat_open_input(&formatContext_, input.c_str(), nullptr, &options);
    av_dict_free(&options);
    if (openResult < 0) {
        close();
        openConvertedInput(input, label);
        return;
    }

    const int infoResult = avformat_find_stream_info(formatContext_, nullptr);
    if (infoResult < 0) {
        close();
        openConvertedInput(input, label);
        return;
    }

    audioStreamIndex_ = av_find_best_stream(formatContext_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audioStreamIndex_ < 0) {
        close();
        openConvertedInput(input, label);
        return;
    }

    AVStream* stream = formatContext_->streams[audioStreamIndex_];
    durationSeconds_ = inferDurationSeconds(formatContext_, stream);
    live_ = durationSeconds_ <= 0.0;

    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) {
        close();
        openConvertedInput(input, label);
        return;
    }

    codecContext_ = avcodec_alloc_context3(codec);
    if (codecContext_ == nullptr) {
        throw std::runtime_error("Cannot allocate streaming decoder context.");
    }

    const int parameterResult = avcodec_parameters_to_context(codecContext_, stream->codecpar);
    if (parameterResult < 0) {
        throw std::runtime_error("Cannot copy codec parameters: " + ffmpegError(parameterResult));
    }

    const int codecOpenResult = avcodec_open2(codecContext_, codec, nullptr);
    if (codecOpenResult < 0) {
        throw std::runtime_error("Cannot open streaming decoder: " + ffmpegError(codecOpenResult));
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

void AudioStreamDecoder::openConvertedInput(const std::string& input, const std::string& label) {
    close();

    std::string converterInput = input;
    durationSeconds_ = 0.0;
    live_ = true;
    if (YtDlpResolver::isYouTubeUrl(input)) {
        const auto media = YtDlpResolver::resolve(input);
        converterInput = media.url;
        durationSeconds_ = media.durationSeconds;
        live_ = durationSeconds_ <= 0.0;
        if (converterInput.empty()) {
            throw std::runtime_error("Cannot resolve YouTube audio URL. Install yt-dlp or refresh the stream URL.");
        }
    }

    const std::vector<std::string> args = {
        "ffmpeg",
        "-nostdin",
        "-hide_banner",
        "-loglevel",
        "error",
        "-reconnect",
        "1",
        "-reconnect_streamed",
        "1",
        "-reconnect_delay_max",
        "5",
        "-protocol_whitelist",
        "file,http,https,tcp,tls,crypto,pipe,data",
        "-user_agent",
        "RetroWave/0.1",
        "-i",
        converterInput,
        "-vn",
        "-f",
        "s16le",
        "-ac",
        "2",
        "-ar",
        "44100",
        "pipe:1",
    };

    if (!converterProcess_.startStdout(args)) {
        throw std::runtime_error("Cannot start ffmpeg converter for " + label);
    }

    eof_ = false;
    readEof_ = false;
    flushSent_ = false;
    clearPendingSamples();
}

void AudioStreamDecoder::requestStop() {
    converterProcess_.requestStop();
}

void AudioStreamDecoder::close() {
    clearPendingSamples();

    converterProcess_.terminate();

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
    live_ = false;
    readEof_ = false;
    flushSent_ = false;
    durationSeconds_ = 0.0;
}

std::size_t AudioStreamDecoder::readFrames(std::int16_t* destination, std::size_t maxFrames) {
    if (destination == nullptr || maxFrames == 0) {
        return 0;
    }

    if (converterProcess_.active()) {
        return readConvertedFrames(destination, maxFrames);
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

std::size_t AudioStreamDecoder::readConvertedFrames(std::int16_t* destination, std::size_t maxFrames) {
    FILE* stream = converterProcess_.stdoutStream();
    if (stream == nullptr) {
        eof_ = true;
        return 0;
    }

    const std::size_t samplesRequested = maxFrames * static_cast<std::size_t>(kOutputChannels);
    const std::size_t samplesRead = std::fread(destination, sizeof(std::int16_t), samplesRequested, stream);
    if (samplesRead < samplesRequested) {
        eof_ = true;
    }
    return samplesRead / static_cast<std::size_t>(kOutputChannels);
}

bool AudioStreamDecoder::eof() const noexcept {
    return eof_;
}

double AudioStreamDecoder::durationSeconds() const noexcept {
    return durationSeconds_;
}

bool AudioStreamDecoder::live() const noexcept {
    return live_;
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
    pendingSamples_.shrink_to_fit();
    pendingOffset_ = 0;
}

AVFormatContext* AudioStreamDecoder::formatContext() const noexcept {
    return formatContext_;
}

int AudioStreamDecoder::audioStreamIndex() const noexcept {
    return audioStreamIndex_;
}

}  // namespace retrowave
