#include "core/Lyrics.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>

namespace retrowave {
namespace {

constexpr std::size_t kMaxLrcFileBytes = 512 * 1024;
constexpr std::size_t kMaxLyricLines = 3000;

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

std::string stripBom(const std::string& content) {
    if (content.size() >= 3 &&
        static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF) {
        return content.substr(3);
    }
    return content;
}

std::string normalizeLineEndings(const std::string& content) {
    std::string result;
    result.reserve(content.size());
    for (std::size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\r') {
            result.push_back('\n');
            if (i + 1 < content.size() && content[i + 1] == '\n') {
                ++i;
            }
        } else {
            result.push_back(content[i]);
        }
    }
    return result;
}

double parseTimestampSeconds(const std::smatch& match) {
    const int hours = match[1].matched ? std::stoi(match[1].str()) : 0;
    const int minutes = std::stoi(match[2].str());
    const int seconds = std::stoi(match[3].str());

    int millis = 0;
    if (match[4].matched) {
        std::string fraction = match[4].str();
        while (fraction.size() < 3) {
            fraction.push_back('0');
        }
        millis = std::stoi(fraction.substr(0, 3));
    }

    return static_cast<double>((hours * 3600) + (minutes * 60) + seconds) + static_cast<double>(millis) / 1000.0;
}

std::vector<LyricSegment> parseInlineSegments(const std::string& text) {
    // Exact same group structure as timestampPattern for parseTimestampSeconds
    static const std::regex inlineTagPattern(R"(<(?:(\d{1,2}):)?(\d{1,2}):(\d{2})(?:[.:](\d{1,3}))?>)");

    std::vector<LyricSegment> segments;
    std::sregex_iterator it(text.begin(), text.end(), inlineTagPattern);
    std::sregex_iterator end;

    std::size_t lastEnd = 0;
    for (; it != end; ++it) {
        const auto& match = *it;
        const double timestamp = parseTimestampSeconds(match);
        const std::size_t matchStart = static_cast<std::size_t>(match.position());
        const std::size_t matchEnd = matchStart + static_cast<std::size_t>(match.length());

        // Any text before this inline tag that wasn't part of a previous segment
        if (matchStart > lastEnd && !segments.empty()) {
            const std::string precedingText = trim(text.substr(lastEnd, matchStart - lastEnd));
            if (!precedingText.empty()) {
                segments.back().text += (segments.back().text.empty() ? "" : " ") + precedingText;
            }
        }

        LyricSegment segment;
        segment.timestampSeconds = timestamp;
        segments.push_back(std::move(segment));

        lastEnd = matchEnd;
    }

    // Collect remaining text after the last inline tag
    if (!segments.empty() && lastEnd < text.size()) {
        const std::string remaining = trim(text.substr(lastEnd));
        if (!remaining.empty()) {
            segments.back().text = remaining;
        }
    }

    // Assign text to segments that don't have it yet by looking ahead
    // Each segment's text runs from its tag to the next tag or end of line
    if (segments.size() > 1) {
        std::vector<std::pair<double, std::size_t>> tagPositions;
        std::sregex_iterator it2(text.begin(), text.end(), inlineTagPattern);
        for (; it2 != end; ++it2) {
            const auto& match = *it2;
            tagPositions.emplace_back(
                parseTimestampSeconds(match),
                static_cast<std::size_t>(match.position() + match.length()));
        }
        
        segments.clear();
        for (std::size_t i = 0; i < tagPositions.size(); ++i) {
            LyricSegment segment;
            segment.timestampSeconds = tagPositions[i].first;

            std::size_t textStart = tagPositions[i].second;
            std::size_t textEnd = (i + 1 < tagPositions.size())
                                      ? static_cast<std::size_t>(text.find('<', textStart))
                                      : text.size();
            if (textEnd == std::string::npos) {
                textEnd = text.size();
            }

            segment.text = trim(text.substr(textStart, textEnd - textStart));
            segments.push_back(std::move(segment));
        }
    }

    // Filter out empty segments
    segments.erase(
        std::remove_if(segments.begin(), segments.end(),
                       [](const LyricSegment& seg) { return seg.text.empty(); }),
        segments.end());

    return segments;
}

bool parseMetadataTag(const std::string& line, std::string& key, std::string& value) {
    static const std::regex metadataPattern(R"(\[([A-Za-z][A-Za-z0-9_-]*):(.*?)\])");
    std::smatch match;
    if (!std::regex_search(line, match, metadataPattern)) {
        return false;
    }

    key = toLower(trim(match[1].str()));
    value = trim(match[2].str());
    return !key.empty();
}

double parseOffsetSeconds(const std::string& value) {
    try {
        return static_cast<double>(std::stoi(trim(value))) / 1000.0;
    } catch (...) {
        return 0.0;
    }
}

std::vector<std::filesystem::path> collectCandidates(const std::filesystem::path& audioPath) {
    std::vector<std::filesystem::path> candidates;
    const auto sidecar = audioPath.parent_path() / (audioPath.stem().string() + ".lrc");
    candidates.push_back(sidecar);
    if (std::filesystem::exists(sidecar)) {
        return candidates;
    }

    const auto directory = audioPath.parent_path();
    if (!std::filesystem::exists(directory)) {
        return candidates;
    }

    const auto targetStem = toLower(audioPath.stem().string());
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const auto ext = toLower(entry.path().extension().string());
        if (ext != ".lrc") {
            continue;
        }

        if (toLower(entry.path().stem().string()) == targetStem) {
            candidates.push_back(entry.path());
        }
    }

    // Also check for .txt lyrics files as fallback
    const auto txtCandidate = audioPath.parent_path() / (audioPath.stem().string() + ".txt");
    if (std::filesystem::exists(txtCandidate)) {
        candidates.push_back(txtCandidate);
    }

    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    return candidates;
}

void computeEndTimestamps(LyricsData& lyrics) {
    if (!lyrics.timed || lyrics.lines.empty()) {
        return;
    }

    for (std::size_t i = 0; i + 1 < lyrics.lines.size(); ++i) {
        lyrics.lines[i].endTimestampSeconds = lyrics.lines[i + 1].timestampSeconds;
    }
    // Last line: estimate 5 seconds or leave at -1
    auto& last = lyrics.lines.back();
    if (last.endTimestampSeconds < 0.0) {
        last.endTimestampSeconds = last.timestampSeconds + 5.0;
    }

    // For word-level segments, set end timestamps
    for (auto& line : lyrics.lines) {
        if (!line.hasWordSync()) {
            continue;
        }
        for (std::size_t j = 0; j + 1 < line.segments.size(); ++j) {
            // segment end = next segment start (no gap)
            // We don't store endTimestamp per segment, but the UI can infer it
        }
    }
}

LyricsData parseLrcContent(std::string rawContent, const std::string& source, const std::string& successMessage) {
    if (rawContent.size() > kMaxLrcFileBytes) {
        rawContent.resize(kMaxLrcFileBytes);
    }
    const std::string content = normalizeLineEndings(stripBom(std::move(rawContent)));

    LyricsData lyrics;
    lyrics.found = true;
    lyrics.sourcePath = source;

    std::istringstream input(content);

    static const std::regex timestampPattern(R"(\[(?:(\d{1,2}):)?(\d{1,2}):(\d{2})(?:[.:](\d{1,3}))?\])");

    std::vector<std::string> plainLines;
    double offsetSeconds = 0.0;
    std::string line;
    while (std::getline(input, line)) {
        std::string metadataKey;
        std::string metadataValue;
        if (parseMetadataTag(line, metadataKey, metadataValue)) {
            lyrics.metadata[metadataKey] = metadataValue;
            if (metadataKey == "offset") {
                offsetSeconds = parseOffsetSeconds(metadataValue);
            }
            continue;
        }

        std::vector<double> timestamps;
        std::size_t lastMatchEnd = 0;

        for (std::sregex_iterator it(line.begin(), line.end(), timestampPattern), end; it != end; ++it) {
            const auto& match = *it;
            timestamps.push_back(parseTimestampSeconds(match));
            lastMatchEnd = static_cast<std::size_t>(match.position() + match.length());
        }

        std::string text = trim(line.substr(lastMatchEnd));
        if (!timestamps.empty()) {
            lyrics.timed = true;

            // Check for Enhanced LRC inline word-level tags <mm:ss.xx>
            auto segments = parseInlineSegments(text);
            if (!segments.empty()) {
                lyrics.enhanced = true;
            }

            // Strip inline tags from the display text
            std::string cleanText = text;
            if (!segments.empty()) {
                static const std::regex inlineTagStrip(R"(<(?:\d{1,2}:)?\d{1,2}:\d{2}(?:[.:]\d{1,3})?>)");
                cleanText = trim(std::regex_replace(text, inlineTagStrip, ""));
            }

            for (double timestamp : timestamps) {
                if (lyrics.lines.size() >= kMaxLyricLines) {
                    break;
                }
                LyricLine lyricLine;
                lyricLine.timestampSeconds = timestamp;
                lyricLine.text = cleanText.empty() ? "..." : cleanText;
                lyricLine.segments = segments;
                lyrics.lines.push_back(std::move(lyricLine));
            }
            continue;
        }

        const std::string cleaned = trim(line);
        if (!cleaned.empty() && !(cleaned.size() > 1 && cleaned.front() == '[' && cleaned.find(':') != std::string::npos)) {
            if (plainLines.size() < kMaxLyricLines) {
                plainLines.push_back(cleaned);
            }
        }
    }

    if (lyrics.lines.empty() && !plainLines.empty()) {
        for (const auto& plainLine : plainLines) {
            if (lyrics.lines.size() >= kMaxLyricLines) {
                break;
            }
            lyrics.lines.push_back(LyricLine{-1.0, -1.0, plainLine, {}});
        }
    }

    if (lyrics.lines.empty()) {
        lyrics.found = false;
        lyrics.message.clear();
        return lyrics;
    }

    if (lyrics.timed && offsetSeconds != 0.0) {
        for (auto& lyricLine : lyrics.lines) {
            lyricLine.timestampSeconds += offsetSeconds;
            // Also offset word-level segments
            for (auto& seg : lyricLine.segments) {
                seg.timestampSeconds += offsetSeconds;
            }
        }
    }

    std::sort(lyrics.lines.begin(), lyrics.lines.end(), [](const LyricLine& left, const LyricLine& right) {
        return left.timestampSeconds < right.timestampSeconds;
    });

    computeEndTimestamps(lyrics);

    std::string suffix;
    if (lyrics.enhanced) {
        suffix = " [enhanced]";
    } else if (lyrics.timed) {
        suffix = " [synced]";
    }
    lyrics.message = successMessage + suffix;
    return lyrics;
}

LyricsData parseLrcFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        LyricsData lyrics;
        lyrics.found = true;
        lyrics.sourcePath = path.string();
        lyrics.message = "Lyrics file exists but could not be opened.";
        return lyrics;
    }

    std::string content;
    content.reserve(4096);
    std::string line;
    while (std::getline(input, line)) {
        if (content.size() + line.size() + 1 > kMaxLrcFileBytes) {
            break;
        }
        content.append(line);
        content.push_back('\n');
    }

    auto lyrics = parseLrcContent(content, path.string(), "Lyrics loaded from " + path.filename().string());
    if (!lyrics.found) {
        lyrics.found = true;
        lyrics.sourcePath = path.string();
        lyrics.message = "Lyrics file was found, but it does not contain readable lines.";
    }
    return lyrics;
}

}  // namespace

LyricsData LyricsLoader::loadForTrack(
    const std::filesystem::path& audioPath,
    const std::string& embeddedLyrics) const {
    for (const auto& candidate : collectCandidates(audioPath)) {
        if (std::filesystem::exists(candidate)) {
            return parseLrcFile(candidate);
        }
    }

    if (!trim(embeddedLyrics).empty()) {
        auto lyrics = parseLrcContent(
            std::move(embeddedLyrics),
            "embedded metadata",
            "Lyrics loaded from embedded metadata.");
        if (lyrics.found) {
            return lyrics;
        }
    }

    LyricsData lyrics;
    lyrics.message = "Could not find .lrc lyrics for this track.";
    return lyrics;
}

}  // namespace retrowave
