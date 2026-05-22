#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace retrowave {

struct LyricSegment {
    double timestampSeconds = -1.0;
    std::string text;
};

struct LyricLine {
    double timestampSeconds = -1.0;
    double endTimestampSeconds = -1.0;
    std::string text;
    std::vector<LyricSegment> segments;

    [[nodiscard]] bool hasWordSync() const noexcept { return !segments.empty(); }
};

struct LyricsData {
    bool found = false;
    bool timed = false;
    bool enhanced = false;
    std::string sourcePath;
    std::string message;
    std::map<std::string, std::string> metadata;
    std::vector<LyricLine> lines;
};

class LyricsLoader {
  public:
    [[nodiscard]] LyricsData loadForTrack(
        const std::filesystem::path& audioPath,
        const std::string& embeddedLyrics = {}) const;
};

}  // namespace retrowave
