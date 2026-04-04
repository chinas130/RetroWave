#include "core/Lyrics.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>

namespace retrowave {
namespace {

std::string trim(std::string value) {
    const auto notSpace = [](unsigned char symbol) { return !std::isspace(symbol); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char symbol) {
        return static_cast<char>(std::tolower(symbol));
    });
    return value;
}

std::vector<std::filesystem::path> collectCandidates(const std::filesystem::path& audioPath) {
    std::vector<std::filesystem::path> candidates;
    candidates.push_back(audioPath.parent_path() / (audioPath.stem().string() + ".lrc"));

    const auto directory = audioPath.parent_path();
    if (!std::filesystem::exists(directory)) {
        return candidates;
    }

    const auto targetStem = toLower(audioPath.stem().string());
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file() || toLower(entry.path().extension().string()) != ".lrc") {
            continue;
        }

        if (toLower(entry.path().stem().string()) == targetStem) {
            candidates.push_back(entry.path());
        }
    }

    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    return candidates;
}

LyricsData parseLrcFile(const std::filesystem::path& path) {
    LyricsData lyrics;
    lyrics.found = true;
    lyrics.sourcePath = path.string();

    std::ifstream input(path);
    if (!input) {
        lyrics.message = "Lyrics file exists but could not be opened.";
        return lyrics;
    }

    static const std::regex timestampPattern(R"(\[(\d{1,2}):(\d{2})(?:[.:](\d{1,3}))?\])");

    std::vector<std::string> plainLines;
    std::string line;
    while (std::getline(input, line)) {
        std::vector<double> timestamps;
        std::size_t lastMatchEnd = 0;

        for (std::sregex_iterator it(line.begin(), line.end(), timestampPattern), end; it != end; ++it) {
            const auto& match = *it;
            const int minutes = std::stoi(match[1].str());
            const int seconds = std::stoi(match[2].str());

            int millis = 0;
            if (match[3].matched) {
                std::string fraction = match[3].str();
                while (fraction.size() < 3) {
                    fraction.push_back('0');
                }
                millis = std::stoi(fraction.substr(0, 3));
            }

            timestamps.push_back(static_cast<double>(minutes * 60 + seconds) + static_cast<double>(millis) / 1000.0);
            lastMatchEnd = static_cast<std::size_t>(match.position() + match.length());
        }

        const std::string text = trim(line.substr(lastMatchEnd));
        if (!timestamps.empty()) {
            lyrics.timed = true;
            for (double timestamp : timestamps) {
                lyrics.lines.push_back(LyricLine{timestamp, text.empty() ? "..." : text});
            }
            continue;
        }

        const std::string cleaned = trim(line);
        if (!cleaned.empty() && !(cleaned.size() > 1 && cleaned.front() == '[' && cleaned.find(':') != std::string::npos)) {
            plainLines.push_back(cleaned);
        }
    }

    if (lyrics.lines.empty() && !plainLines.empty()) {
        for (const auto& plainLine : plainLines) {
            lyrics.lines.push_back(LyricLine{-1.0, plainLine});
        }
    }

    if (lyrics.lines.empty()) {
        lyrics.message = "Lyrics file was found, but it does not contain readable lines.";
        return lyrics;
    }

    std::sort(lyrics.lines.begin(), lyrics.lines.end(), [](const LyricLine& left, const LyricLine& right) {
        return left.timestampSeconds < right.timestampSeconds;
    });

    lyrics.message = "Lyrics loaded from " + path.filename().string();
    return lyrics;
}

}  // namespace

LyricsData LyricsLoader::loadForTrack(const std::filesystem::path& audioPath) const {
    for (const auto& candidate : collectCandidates(audioPath)) {
        if (std::filesystem::exists(candidate)) {
            return parseLrcFile(candidate);
        }
    }

    LyricsData lyrics;
    lyrics.message = "Could not find .lrc lyrics for this track.";
    return lyrics;
}

}  // namespace retrowave
