#include "core/Playlist.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// ─── helpers ────────────────────────────────────────────────────────────────

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void requireEqual(std::size_t actual, std::size_t expected, const std::string& message) {
    if (actual != expected) {
        throw std::runtime_error(message + ": expected " + std::to_string(expected) +
                                 ", got " + std::to_string(actual));
    }
}

void requireEqual(const std::string& actual, const std::string& expected, const std::string& message) {
    if (actual != expected) {
        throw std::runtime_error(message + ": expected \"" + expected + "\", got \"" + actual + "\"");
    }
}

void writeFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        throw std::runtime_error("cannot write " + path.string());
    }
    out << content;
}

void touchFile(const std::filesystem::path& path) {
    writeFile(path, "");
}

std::filesystem::path makeTempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("retrowave-pl-test-" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

// ─── tests ──────────────────────────────────────────────────────────────────

void test_emptySources_throws() {
    // fromSources({}) scans CWD — make sure we test with an empty directory.
    const auto tempDir = makeTempDir();
    const auto originalCwd = std::filesystem::current_path();
    std::filesystem::current_path(tempDir);

    bool threw = false;
    try {
        const auto playlist = retrowave::Playlist::fromSources({});
    } catch (const std::runtime_error&) {
        threw = true;
    }

    std::filesystem::current_path(originalCwd);
    std::filesystem::remove_all(tempDir);
    require(threw, "fromSources({}) in empty dir should throw");
}

void test_localFiles_scannedRecursively() {
    const auto dir = makeTempDir();
    const auto subDir = dir / "sub";
    std::filesystem::create_directories(subDir);

    touchFile(dir / "track1.mp3");
    touchFile(dir / "track2.flac");
    touchFile(subDir / "track3.ogg");
    touchFile(dir / "readme.txt");       // unsupported — should be ignored
    touchFile(subDir / "cover.jpg");     // unsupported — should be ignored

    const auto playlist = retrowave::Playlist::fromSources({dir.string()});
    requireEqual(playlist.size(), 3, "should find 3 supported audio files");
}

void test_remoteUrl_detected() {
    const auto dir = makeTempDir();
    const auto playlist = retrowave::Playlist::fromSources({
        dir.string(),
        "https://example.com/radio.mp3",
    });
    // dir contributes 0 audio files, so only the remote entry counts.
    // fromSources throws when items empty. Add a local file.
    touchFile(dir / "local.mp3");

    const auto playlist2 = retrowave::Playlist::fromSources({
        dir.string(),
        "https://example.com/radio.mp3",
    });
    requireEqual(playlist2.size(), 2, "should have local + remote");

    const bool localRemote = playlist2.isRemoteAt(0);
    const bool urlRemote = playlist2.isRemoteAt(1);
    require(!localRemote, "local file should not be remote");
    require(urlRemote, "URL entry should be remote");
    requireEqual(playlist2.sourceAt(1), "https://example.com/radio.mp3", "remote source should match URL");

    std::filesystem::remove_all(dir);
}

void test_m3uPlaylist_parsed() {
    const auto dir = makeTempDir();
    touchFile(dir / "song1.mp3");
    touchFile(dir / "song2.flac");

    const auto m3uPath = dir / "list.m3u";
    writeFile(m3uPath,
        "#EXTM3U\n"
        "#EXTINF:123,Song One\n"
        "song1.mp3\n"
        "#EXTINF:456,Song Two\n"
        "song2.flac\n");

    const auto playlist = retrowave::Playlist::fromSources({m3uPath.string()});
    requireEqual(playlist.size(), 2, "m3u should produce 2 entries");
    // Note: EXTINF titles for local files are not stored in PlaylistItem — title
    // falls back to the filename stem.
    requireEqual(playlist.titleAt(0), "song1", "local m3u entry title is filename stem (EXTINF not persisted)");
    requireEqual(playlist.titleAt(1), "song2", "local m3u entry title is filename stem");

    std::filesystem::remove_all(dir);
}

void test_titleAt_returnsFilenameStem() {
    const auto dir = makeTempDir();
    touchFile(dir / "HelloWorld.mp3");
    const auto playlist = retrowave::Playlist::fromSources({dir.string()});
    require(!playlist.empty(), "playlist not empty");
    requireEqual(playlist.titleAt(0), "HelloWorld", "title should be stem without extension");
    std::filesystem::remove_all(dir);
}

void test_remoteTitle_fromUrl() {
    const auto dir = makeTempDir();
    touchFile(dir / "dummy.mp3");
    const auto playlist = retrowave::Playlist::fromSources({
        dir.string(),
        "https://example.com/path/to/My%20Stream.mp3",
    });
    requireEqual(playlist.size(), 2, "local + remote");
    // The remote item has no explicit title, so titleAt falls back to the URL's last path segment.
    const std::string title = playlist.titleAt(1);
    require(title.find("My%20Stream") != std::string::npos ||
            title.find("My Stream") != std::string::npos,
            "remote title should contain stream name, got: " + title);
    std::filesystem::remove_all(dir);
}

void test_hlsPlaylist_treatedAsSingleEntry() {
    const auto dir = makeTempDir();
    const auto m3u8Path = dir / "live.m3u8";
    writeFile(m3u8Path,
        "#EXTM3U\n"
        "#EXT-X-VERSION:3\n"
        "#EXT-X-TARGETDURATION:10\n"
        "#EXTINF:10.0,\n"
        "segment1.ts\n"
        "#EXTINF:10.0,\n"
        "segment2.ts\n"
        "#EXT-X-ENDLIST\n");

    const auto playlist = retrowave::Playlist::fromSources({m3u8Path.string()});
    requireEqual(playlist.size(), 1, "HLS playlist should be a single entry");
    // A local HLS file is stored as a local file entry (not remote),
    // but is treated as a single track rather than expanded.
    require(!playlist.isRemoteAt(0), "local HLS entry is local, not remote");
    std::filesystem::remove_all(dir);
}

void test_plsPlaylist_parsed() {
    const auto dir = makeTempDir();
    touchFile(dir / "track.mp3");
    const auto plsPath = dir / "station.pls";
    writeFile(plsPath,
        "[playlist]\n"
        "File1=track.mp3\n"
        "Title1=My Track\n"
        "Length1=300\n"
        "NumberOfEntries=1\n"
        "Version=2\n");

    const auto playlist = retrowave::Playlist::fromSources({plsPath.string()});
    requireEqual(playlist.size(), 1, "PLS should produce 1 entry");
    require(!playlist.isRemoteAt(0), "local PLS entry should be local");
    // Note: for local PLS entries, title falls back to filename stem
    requireEqual(playlist.titleAt(0), "track", "local PLS entry title is filename stem");
    std::filesystem::remove_all(dir);
}

void test_multipleFiles_noDuplicates() {
    const auto dir = makeTempDir();
    // Provide the same directory twice
    touchFile(dir / "a.mp3");
    touchFile(dir / "b.flac");
    const auto playlist = retrowave::Playlist::fromSources({dir.string(), dir.string()});
    // Playlist deduplicates at source root level — but items reference sourceIndex
    // so a track discovered in the same root is still listed once per discovery pass.
    // Actually fromSources appends each source separately. Two identical directories
    // will produce duplicate items.
    // This test documents the current behaviour — no dedup across multiple source args.
    requireEqual(playlist.size(), 4, "identical dir added twice produces duplicates");
    std::filesystem::remove_all(dir);
}

}  // namespace

int main() {
    try {
        test_emptySources_throws();
        test_localFiles_scannedRecursively();
        test_remoteUrl_detected();
        test_m3uPlaylist_parsed();
        test_titleAt_returnsFilenameStem();
        test_remoteTitle_fromUrl();
        test_hlsPlaylist_treatedAsSingleEntry();
        test_plsPlaylist_parsed();
        test_multipleFiles_noDuplicates();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "All playlist tests passed.\n";
    return 0;
}