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
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

namespace retrowave {
namespace {

constexpr int kWaveformBins = 160;
constexpr int kArtMaxWidth = 72;
constexpr int kArtMaxHeight = 48;

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
using SwsContextPtr = std::unique_ptr<SwsContext, decltype(&sws_freeContext)>;

std::vector<float> placeholderWaveform(double durationSeconds) {
    std::vector<float> waveform(kWaveformBins, 0.0F);
    if (waveform.empty()) {
        return waveform;
    }

    const float seed = static_cast<float>(std::fmod(std::max(1.0, durationSeconds), 17.0) / 17.0);
    for (std::size_t bin = 0; bin < waveform.size(); ++bin) {
        const float position = static_cast<float>(bin) / static_cast<float>(waveform.size() - 1);
        const float envelope = 0.18F + 0.1F * std::sin((position * 7.0F + seed) * 3.1415926F);
        waveform[bin] = std::clamp(envelope, 0.06F, 0.3F);
    }

    return waveform;
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

TrackMetadata scanTrackMetadata(AVFormatContext* formatContext,
                               const AVStream* audioStream,
                               const std::filesystem::path& path) {
    TrackMetadata metadata;
    metadata.path = path;
    metadata.title = path.stem().string();
    metadata.artist = metadataValue(formatContext->metadata, "artist");
    metadata.album = metadataValue(formatContext->metadata, "album");

    const std::string title = metadataValue(formatContext->metadata, "title");
    if (!title.empty()) {
        metadata.title = title;
    }

    LyricsLoader lyricsLoader;
    metadata.albumArt = extractAlbumArt(formatContext, path);
    metadata.lyrics = std::make_shared<LyricsData>(lyricsLoader.loadForTrack(path));
    metadata.durationSeconds = inferDurationSeconds(formatContext, audioStream);
    metadata.waveform = placeholderWaveform(metadata.durationSeconds);
    return metadata;
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
    DecodedTrack track;
    track.metadata = scanTrackMetadata(formatContext.get(), stream, path);
    return track;
}

}  // namespace retrowave
