#include "audio/AudioDecoder.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace retrowave {
namespace {

constexpr int kOutputSampleRate = 44100;
constexpr int kOutputChannels = 2;
constexpr int kWaveformBins = 160;
constexpr int kArtMaxWidth = 72;
constexpr int kArtMaxHeight = 48;
constexpr float kInt16Scale = 32767.0F;

std::string ffmpegError(int errorCode) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(errorCode, buffer, sizeof(buffer));
    return std::string(buffer);
}

template <typename T, void (*Destroy)(T**)>
using ScopedPtr = std::unique_ptr<T, decltype([](T* value) {
    if (value != nullptr) {
        Destroy(&value);
    }
})>;

using FormatContextPtr = ScopedPtr<AVFormatContext, avformat_close_input>;
using CodecContextPtr = ScopedPtr<AVCodecContext, avcodec_free_context>;
using FramePtr = ScopedPtr<AVFrame, av_frame_free>;
using PacketPtr = ScopedPtr<AVPacket, av_packet_free>;
using SwrContextPtr = ScopedPtr<SwrContext, swr_free>;
using SwsContextPtr = std::unique_ptr<SwsContext, decltype(&sws_freeContext)>;

std::vector<float> buildWaveform(const std::vector<std::int16_t>& samples, int channels) {
    std::vector<float> waveform(kWaveformBins, 0.0f);
    if (samples.empty() || channels <= 0) {
        return waveform;
    }

    const std::size_t totalFrames = samples.size() / static_cast<std::size_t>(channels);
    const std::size_t framesPerBin = std::max<std::size_t>(1, totalFrames / waveform.size());

    for (std::size_t bin = 0; bin < waveform.size(); ++bin) {
        const std::size_t begin = bin * framesPerBin;
        const std::size_t end = std::min(totalFrames, begin + framesPerBin);

        float peak = 0.0f;
        for (std::size_t frame = begin; frame < end; ++frame) {
            for (int channel = 0; channel < channels; ++channel) {
                const auto sampleIndex = frame * static_cast<std::size_t>(channels) + static_cast<std::size_t>(channel);
                peak = std::max(peak, std::abs(static_cast<float>(samples[sampleIndex]) / kInt16Scale));
            }
        }

        waveform[bin] = peak;
    }

    return waveform;
}

std::string metadataValue(AVDictionary* metadata, const char* key) {
    AVDictionaryEntry* entry = av_dict_get(metadata, key, nullptr, 0);
    if (entry == nullptr || entry->value == nullptr) {
        return {};
    }
    return entry->value;
}

AlbumArt convertFrameToAlbumArt(const AVFrame* frame) {
    const double widthScale = static_cast<double>(kArtMaxWidth) / static_cast<double>(std::max(1, frame->width));
    const double heightScale = static_cast<double>(kArtMaxHeight) / static_cast<double>(std::max(1, frame->height));
    const double scale = std::min({1.0, widthScale, heightScale});

    AlbumArt art;
    art.width = std::max(1, static_cast<int>(std::round(frame->width * scale)));
    art.height = std::max(1, static_cast<int>(std::round(frame->height * scale)));
    art.grayscale.resize(static_cast<std::size_t>(art.width * art.height));

    SwsContextPtr scaler(
        sws_getContext(
            frame->width,
            frame->height,
            static_cast<AVPixelFormat>(frame->format),
            art.width,
            art.height,
            AV_PIX_FMT_GRAY8,
            SWS_LANCZOS,
            nullptr,
            nullptr,
            nullptr),
        &sws_freeContext);

    if (!scaler) {
        throw std::runtime_error("Cannot create scaler for album art.");
    }

    uint8_t* destination[] = {art.grayscale.data()};
    int linesize[] = {art.width};
    sws_scale(scaler.get(), frame->data, frame->linesize, 0, frame->height, destination, linesize);
    return art;
}

std::shared_ptr<const AlbumArt> decodeStillImage(AVFormatContext* formatContext, int streamIndex, const AVPacket* packetSource) {
    AVStream* stream = formatContext->streams[streamIndex];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) {
        return nullptr;
    }

    CodecContextPtr codecContext(avcodec_alloc_context3(codec));
    FramePtr frame(av_frame_alloc());
    if (!codecContext || !frame) {
        return nullptr;
    }

    const int parametersResult = avcodec_parameters_to_context(codecContext.get(), stream->codecpar);
    if (parametersResult < 0) {
        return nullptr;
    }

    if (avcodec_open2(codecContext.get(), codec, nullptr) < 0) {
        return nullptr;
    }

    if (avcodec_send_packet(codecContext.get(), packetSource) < 0) {
        return nullptr;
    }

    if (avcodec_receive_frame(codecContext.get(), frame.get()) < 0) {
        return nullptr;
    }

    return std::make_shared<AlbumArt>(convertFrameToAlbumArt(frame.get()));
}

std::shared_ptr<const AlbumArt> loadAlbumArtFromFile(const std::filesystem::path& path) {
    AVFormatContext* rawFormatContext = nullptr;
    const std::string pathString = path.string();
    if (avformat_open_input(&rawFormatContext, pathString.c_str(), nullptr, nullptr) < 0) {
        return nullptr;
    }

    FormatContextPtr formatContext(rawFormatContext);
    if (avformat_find_stream_info(formatContext.get(), nullptr) < 0) {
        return nullptr;
    }

    const int streamIndex = av_find_best_stream(formatContext.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (streamIndex < 0) {
        return nullptr;
    }

    PacketPtr packet(av_packet_alloc());
    if (!packet) {
        return nullptr;
    }

    while (av_read_frame(formatContext.get(), packet.get()) >= 0) {
        if (packet->stream_index == streamIndex) {
            auto art = decodeStillImage(formatContext.get(), streamIndex, packet.get());
            av_packet_unref(packet.get());
            return art;
        }
        av_packet_unref(packet.get());
    }

    return nullptr;
}

std::shared_ptr<const AlbumArt> extractAlbumArt(AVFormatContext* formatContext, const std::filesystem::path& audioPath) {
    for (unsigned int streamIndex = 0; streamIndex < formatContext->nb_streams; ++streamIndex) {
        AVStream* stream = formatContext->streams[streamIndex];
        if ((stream->disposition & AV_DISPOSITION_ATTACHED_PIC) == 0) {
            continue;
        }

        if (auto art = decodeStillImage(formatContext, static_cast<int>(streamIndex), &stream->attached_pic)) {
            return art;
        }
    }

    static const std::vector<std::string> kCoverNames = {
        "cover.jpg", "cover.jpeg", "cover.png",
        "folder.jpg", "folder.jpeg", "folder.png",
        "front.jpg", "front.jpeg", "front.png",
        "album.jpg", "album.jpeg", "album.png"
    };

    for (const auto& name : kCoverNames) {
        const auto candidate = audioPath.parent_path() / name;
        if (!std::filesystem::exists(candidate)) {
            continue;
        }
        if (auto art = loadAlbumArtFromFile(candidate)) {
            return art;
        }
    }

    return nullptr;
}

void decodeFrames(AVCodecContext* codecContext,
                  AVFormatContext* formatContext,
                  int audioStreamIndex,
                  SwrContext* resampler,
                  std::vector<std::int16_t>& samples) {
    PacketPtr packet(av_packet_alloc());
    FramePtr frame(av_frame_alloc());

    if (!packet || !frame) {
        throw std::runtime_error("Failed to allocate FFmpeg packet/frame.");
    }

    auto appendFrame = [&](AVFrame* decodedFrame) {
        const int dstSamples = av_rescale_rnd(
            swr_get_delay(resampler, codecContext->sample_rate) + decodedFrame->nb_samples,
            kOutputSampleRate,
            codecContext->sample_rate,
            AV_ROUND_UP);

        std::vector<std::int16_t> converted(static_cast<std::size_t>(dstSamples) * kOutputChannels);
        uint8_t* outPlanes[] = {reinterpret_cast<uint8_t*>(converted.data())};
        const auto* inPlanes = const_cast<const uint8_t**>(decodedFrame->extended_data);

        const int written = swr_convert(resampler, outPlanes, dstSamples, inPlanes, decodedFrame->nb_samples);
        if (written < 0) {
            throw std::runtime_error("Audio resampling failed: " + ffmpegError(written));
        }

        converted.resize(static_cast<std::size_t>(written) * kOutputChannels);
        samples.insert(samples.end(), converted.begin(), converted.end());
    };

    auto receiveFrames = [&]() {
        while (true) {
            const int receiveResult = avcodec_receive_frame(codecContext, frame.get());
            if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
                return;
            }
            if (receiveResult < 0) {
                throw std::runtime_error("Decoder receive error: " + ffmpegError(receiveResult));
            }

            appendFrame(frame.get());
            av_frame_unref(frame.get());
        }
    };

    while (true) {
        const int readResult = av_read_frame(formatContext, packet.get());
        if (readResult == AVERROR_EOF) {
            break;
        }
        if (readResult < 0) {
            throw std::runtime_error("Failed to read audio packet: " + ffmpegError(readResult));
        }

        if (packet->stream_index == audioStreamIndex) {
            const int sendResult = avcodec_send_packet(codecContext, packet.get());
            if (sendResult < 0) {
                throw std::runtime_error("Decoder send error: " + ffmpegError(sendResult));
            }

            receiveFrames();
        }

        av_packet_unref(packet.get());
    }

    const int flushResult = avcodec_send_packet(codecContext, nullptr);
    if (flushResult < 0) {
        throw std::runtime_error("Decoder flush error: " + ffmpegError(flushResult));
    }
    receiveFrames();
}

}  // namespace

DecodedTrack AudioDecoder::decode(const std::filesystem::path& path) const {
    AVFormatContext* rawFormatContext = nullptr;
    const std::string pathString = path.string();
    const int openResult = avformat_open_input(&rawFormatContext, pathString.c_str(), nullptr, nullptr);
    if (openResult < 0) {
        throw std::runtime_error("Cannot open file: " + ffmpegError(openResult));
    }

    FormatContextPtr formatContext(rawFormatContext);

    const int infoResult = avformat_find_stream_info(formatContext.get(), nullptr);
    if (infoResult < 0) {
        throw std::runtime_error("Cannot inspect stream info: " + ffmpegError(infoResult));
    }

    const int audioStreamIndex = av_find_best_stream(formatContext.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audioStreamIndex < 0) {
        throw std::runtime_error("No audio stream found: " + ffmpegError(audioStreamIndex));
    }

    AVStream* stream = formatContext->streams[audioStreamIndex];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) {
        throw std::runtime_error("Unsupported codec for " + path.filename().string());
    }

    CodecContextPtr codecContext(avcodec_alloc_context3(codec));
    if (!codecContext) {
        throw std::runtime_error("Cannot allocate decoder context.");
    }

    const int parameterResult = avcodec_parameters_to_context(codecContext.get(), stream->codecpar);
    if (parameterResult < 0) {
        throw std::runtime_error("Cannot copy codec parameters: " + ffmpegError(parameterResult));
    }

    const int codecOpenResult = avcodec_open2(codecContext.get(), codec, nullptr);
    if (codecOpenResult < 0) {
        throw std::runtime_error("Cannot open decoder: " + ffmpegError(codecOpenResult));
    }

    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, kOutputChannels);

    AVChannelLayout inLayout = codecContext->ch_layout;
    if (inLayout.nb_channels == 0) {
        av_channel_layout_default(&inLayout, 2);
    }

    SwrContext* rawResampler = nullptr;
    const int swrSetupResult = swr_alloc_set_opts2(
        &rawResampler,
        &outLayout,
        AV_SAMPLE_FMT_S16,
        kOutputSampleRate,
        &inLayout,
        codecContext->sample_fmt,
        codecContext->sample_rate,
        0,
        nullptr);

    if (swrSetupResult < 0 || rawResampler == nullptr) {
        av_channel_layout_uninit(&outLayout);
        throw std::runtime_error("Cannot configure resampler: " + ffmpegError(swrSetupResult));
    }

    SwrContextPtr resampler(rawResampler);

    const int swrInitResult = swr_init(resampler.get());
    if (swrInitResult < 0) {
        av_channel_layout_uninit(&outLayout);
        throw std::runtime_error("Cannot initialize resampler: " + ffmpegError(swrInitResult));
    }

    DecodedTrack track;
    track.path = path;
    track.title = path.stem().string();
    track.sampleRate = kOutputSampleRate;
    track.channels = kOutputChannels;
    track.artist = metadataValue(formatContext->metadata, "artist");
    track.album = metadataValue(formatContext->metadata, "album");

    const std::string title = metadataValue(formatContext->metadata, "title");
    if (!title.empty()) {
        track.title = title;
    }

    LyricsLoader lyricsLoader;
    track.albumArt = extractAlbumArt(formatContext.get(), path);
    track.lyrics = std::make_shared<LyricsData>(lyricsLoader.loadForTrack(path));

    decodeFrames(codecContext.get(), formatContext.get(), audioStreamIndex, resampler.get(), track.samples);

    track.durationSeconds =
        static_cast<double>(track.samples.size()) / static_cast<double>(track.sampleRate * track.channels);
    track.waveform = buildWaveform(track.samples, track.channels);

    av_channel_layout_uninit(&outLayout);
    return track;
}

}  // namespace retrowave
