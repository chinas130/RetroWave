#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace retrowave {

struct LyricLine {
    double timestampSeconds = -1.0;
    std::string text;
};

struct LyricsData {
    bool found = false;
    bool timed = false;
    std::string sourcePath;
    std::string message;
    std::vector<LyricLine> lines;
};

class LyricsLoader {
  public:
    [[nodiscard]] LyricsData loadForTrack(const std::filesystem::path& audioPath) const;
};

}  // namespace retrowave
