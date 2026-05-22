#pragma once

#include "core/Lyrics.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct AVFormatContext;

namespace retrowave {

struct AlbumArt {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> grayscale;

    [[nodiscard]] bool empty() const noexcept {
        return grayscale.empty();
    }
};

struct TrackMetadata {
    std::filesystem::path path;
    std::string source;
    bool remote = false;
    bool live = false;
    std::string title;
    std::string artist;
    std::string album;
    double durationSeconds = 0.0;
    std::vector<float> waveform;
    std::shared_ptr<const AlbumArt> albumArt;
    std::shared_ptr<const LyricsData> lyrics;
    std::string embeddedLyricsScratch;
    bool lyricsResolved = false;
};

struct DecodedTrack {
    TrackMetadata metadata;
};

class AudioDecoder {
  public:
    [[nodiscard]] DecodedTrack decode(const std::filesystem::path& path) const;
    [[nodiscard]] DecodedTrack decodeUrl(const std::string& url, const std::string& titleHint = {}) const;
    [[nodiscard]] TrackMetadata probeLocalMetadata(
        AVFormatContext* formatContext,
        int audioStreamIndex,
        const std::filesystem::path& path) const;
};

}  // namespace retrowave
