#include "audio/AudioDecoder.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
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

std::string normalizedMetadataKey(std::string_view key) {
    std::string normalized;
    normalized.reserve(key.size());
    for (const unsigned char symbol : key) {
        if (symbol == '_' || symbol == '-' || std::isspace(symbol)) {
            continue;
        }
        normalized.push_back(static_cast<char>(std::tolower(symbol)));
    }
    return normalized;
}

bool isLyricsMetadataKey(std::string_view key) {
    const auto normalized = normalizedMetadataKey(key);
    return normalized == "lyrics" ||
        normalized == "lyric" ||
        normalized == "syncedlyrics" ||
        normalized == "unsyncedlyrics" ||
        normalized == "uslt" ||
        normalized == "sylt" ||
        normalized == "lyr" ||
        normalized == "lyricsunsynced" ||
        normalized == "lyricssynced" ||
        normalized == "text";
}

std::string lyricsFromMetadata(AVDictionary* metadata) {
    if (metadata == nullptr) {
        return {};
    }

    // First pass: prefer synced lyrics over unsynced
    AVDictionaryEntry* entry = nullptr;
    std::string unsyncedCandidate;
    while ((entry = av_dict_get(metadata, "", entry, AV_DICT_IGNORE_SUFFIX)) != nullptr) {
        if (entry->key == nullptr || entry->value == nullptr || entry->value[0] == '\0') {
            continue;
        }

        if (!isLyricsMetadataKey(entry->key)) {
            continue;
        }

        const auto normalized = normalizedMetadataKey(entry->key);
        if (normalized == "syncedlyrics" || normalized == "sylt" || normalized == "lyricssynced") {
            return entry->value;
        }
        if (unsyncedCandidate.empty()) {
            unsyncedCandidate = entry->value;
        }
    }
    return unsyncedCandidate;
}

std::string extractEmbeddedLyrics(const AVFormatContext* formatContext, const AVStream* audioStream) {
    // Check format-level metadata first
    if (formatContext != nullptr) {
        if (auto lyrics = lyricsFromMetadata(formatContext->metadata); !lyrics.empty()) {
            return lyrics;
        }
    }

    // Check audio stream metadata
    if (audioStream != nullptr) {
        if (auto lyrics = lyricsFromMetadata(audioStream->metadata); !lyrics.empty()) {
            return lyrics;
        }
    }

    // Search all other streams for lyrics metadata (some formats store lyrics in separate streams)
    if (formatContext != nullptr) {
        for (unsigned int i = 0; i < formatContext->nb_streams; ++i) {
            const AVStream* stream = formatContext->streams[i];
            if (stream == audioStream) {
                continue;
            }
            if (auto lyrics = lyricsFromMetadata(stream->metadata); !lyrics.empty()) {
                return lyrics;
            }
        }
    }

    return {};
}

std::string titleFromUrl(const std::string& url) {
    const auto withoutQuery = url.substr(0, url.find_first_of("?#"));
    const auto slash = withoutQuery.find_last_of('/');
    if (slash != std::string::npos && slash + 1 < withoutQuery.size()) {
        return withoutQuery.substr(slash + 1);
    }
    return url;
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

std::shared_ptr<const AlbumArt> extractAlbumArt(
    AVFormatContext* formatContext,
    const std::filesystem::path& audioPath,
    bool allowLocalFallback) {
    for (unsigned int streamIndex = 0; streamIndex < formatContext->nb_streams; ++streamIndex) {
        AVStream* stream = formatContext->streams[streamIndex];
        if ((stream->disposition & AV_DISPOSITION_ATTACHED_PIC) == 0) {
            continue;
        }

        if (auto art = decodeStillImage(formatContext, static_cast<int>(streamIndex), &stream->attached_pic)) {
            return art;
        }
    }

    if (!allowLocalFallback) {
        return nullptr;
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
                               const std::filesystem::path& path,
                               const std::string& source,
                               bool remote,
                               const std::string& titleHint) {
    TrackMetadata metadata;
    metadata.path = path;
    metadata.source = source;
    metadata.remote = remote;
    metadata.title = remote && !titleHint.empty() ? titleHint : (remote ? titleFromUrl(source) : path.stem().string());
    metadata.artist = metadataValue(formatContext->metadata, "artist");
    metadata.album = metadataValue(formatContext->metadata, "album");

    const std::string title = metadataValue(formatContext->metadata, "title");
    if (!title.empty()) {
        metadata.title = title;
    }

    metadata.durationSeconds = inferDurationSeconds(formatContext, audioStream);
    metadata.live = remote && metadata.durationSeconds <= 0.0;
    metadata.albumArt = extractAlbumArt(formatContext, path, !remote);
    if (remote) {
        auto lyrics = std::make_shared<LyricsData>();
        lyrics->message = "Lyrics are unavailable for remote streams.";
        metadata.lyrics = lyrics;
        metadata.lyricsResolved = true;
    } else {
        metadata.embeddedLyricsScratch = extractEmbeddedLyrics(formatContext, audioStream);
        constexpr std::size_t kMaxEmbeddedLyricsBytes = 512 * 1024;
        if (metadata.embeddedLyricsScratch.size() > kMaxEmbeddedLyricsBytes) {
            metadata.embeddedLyricsScratch.resize(kMaxEmbeddedLyricsBytes);
        }
        auto lyrics = std::make_shared<LyricsData>();
        lyrics->message = "Could not find .lrc lyrics for this track.";
        metadata.lyrics = lyrics;
    }
    metadata.waveform = placeholderWaveform(metadata.durationSeconds);
    return metadata;
}

}  // namespace

namespace {

DecodedTrack decodeInput(
    const std::string& input,
    const std::filesystem::path& path,
    bool remote,
    const std::string& titleHint) {
    AVFormatContext* rawFormatContext = nullptr;
    const int openResult = avformat_open_input(&rawFormatContext, input.c_str(), nullptr, nullptr);
    if (openResult < 0) {
        throw std::runtime_error(std::string(remote ? "Cannot open remote stream: " : "Cannot open file: ") + ffmpegError(openResult));
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
    track.metadata = scanTrackMetadata(formatContext.get(), stream, path, input, remote, titleHint);
    return track;
}

}  // namespace

DecodedTrack AudioDecoder::decode(const std::filesystem::path& path) const {
    return decodeInput(path.string(), path, false, {});
}

DecodedTrack AudioDecoder::decodeUrl(const std::string& url, const std::string& titleHint) const {
    return decodeInput(url, std::filesystem::path(url), true, titleHint);
}

TrackMetadata AudioDecoder::probeLocalMetadata(
    AVFormatContext* formatContext,
    int audioStreamIndex,
    const std::filesystem::path& path) const {
    if (formatContext == nullptr || audioStreamIndex < 0 ||
        static_cast<unsigned int>(audioStreamIndex) >= formatContext->nb_streams) {
        throw std::runtime_error("Cannot read metadata from an invalid audio stream.");
    }

    return scanTrackMetadata(
        formatContext,
        formatContext->streams[audioStreamIndex],
        path,
        path.string(),
        false,
        {});
}

}  // namespace retrowave
