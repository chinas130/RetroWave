#pragma once

#include "core/Lyrics.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace retrowave {

struct AlbumArt {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> grayscale;

    [[nodiscard]] bool empty() const noexcept {
        return grayscale.empty();
    }
};

struct DecodedTrack {
    std::filesystem::path path;
    std::string title;
    std::string artist;
    std::string album;
    int sampleRate = 44100;
    int channels = 2;
    double durationSeconds = 0.0;
    std::vector<std::int16_t> samples;
    std::vector<float> waveform;
    std::shared_ptr<const AlbumArt> albumArt;
    std::shared_ptr<const LyricsData> lyrics;
};

class AudioDecoder {
  public:
    [[nodiscard]] DecodedTrack decode(const std::filesystem::path& path) const;
};

}  // namespace retrowave
