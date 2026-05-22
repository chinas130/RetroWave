#include "core/Lyrics.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void requireNear(double actual, double expected, const std::string& message) {
    if (std::abs(actual - expected) > 0.001) {
        throw std::runtime_error(message + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
    }
}

void writeText(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot create " + path.string());
    }
    output << content;
}

std::filesystem::path makeTempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("retrowave-lyrics-test-" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

void testSidecarLrc() {
    const auto directory = makeTempDir();
    const auto track = directory / "song.mp3";
    const auto lrc = directory / "song.lrc";
    writeText(
        lrc,
        "[ar:Artist]\n"
        "[ti:Title]\n"
        "[al:Album]\n"
        "[by:RetroWave]\n"
        "[length:01:23]\n"
        "[offset:+500]\n"
        "[00:01.00][00:02.250]Repeated line\n"
        "[01:02:03.004]Long song line\n");

    const auto lyrics = retrowave::LyricsLoader{}.loadForTrack(track, "[00:00.00]embedded should not win");
    require(lyrics.found, "sidecar lyrics should be found");
    require(lyrics.timed, "sidecar lyrics should be timed");
    require(lyrics.lines.size() == 3, "sidecar lyrics should expand repeated timestamps");
    require(lyrics.metadata.at("ar") == "Artist", "artist metadata should be parsed");
    require(lyrics.metadata.at("ti") == "Title", "title metadata should be parsed");
    require(lyrics.metadata.at("al") == "Album", "album metadata should be parsed");
    requireNear(lyrics.lines[0].timestampSeconds, 1.5, "offset should shift first timestamp");
    requireNear(lyrics.lines[1].timestampSeconds, 2.75, "offset should shift second timestamp");
    requireNear(lyrics.lines[2].timestampSeconds, 3723.504, "hour timestamp should be parsed");
    require(lyrics.lines[0].text == "Repeated line", "lyric text should be parsed");

    std::filesystem::remove_all(directory);
}

void testEmbeddedFallback() {
    const auto directory = makeTempDir();
    const auto track = directory / "song.mp3";

    const auto lyrics = retrowave::LyricsLoader{}.loadForTrack(
        track,
        "[offset:-250]\n"
        "[00:00.500]Embedded line\n");

    require(lyrics.found, "embedded lyrics should be used when sidecar is missing");
    require(lyrics.timed, "embedded lyrics should be timed");
    require(lyrics.sourcePath == "embedded metadata", "embedded source should be labeled");
    require(lyrics.lines.size() == 1, "embedded lyrics should produce one line");
    requireNear(lyrics.lines[0].timestampSeconds, 0.25, "negative offset should be applied");
    require(lyrics.lines[0].text == "Embedded line", "embedded lyric text should be parsed");

    std::filesystem::remove_all(directory);
}

void testMissingFallbackMessage() {
    const auto directory = makeTempDir();
    const auto track = directory / "song.mp3";

    const auto lyrics = retrowave::LyricsLoader{}.loadForTrack(track, "[ar:Only metadata]");
    require(!lyrics.found, "metadata-only embedded lyrics should not count as found lyrics");
    require(lyrics.message == "Could not find .lrc lyrics for this track.", "missing message should stay sidecar-focused");

    std::filesystem::remove_all(directory);
}

}  // namespace

int main() {
    try {
        testSidecarLrc();
        testEmbeddedFallback();
        testMissingFallbackMessage();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
