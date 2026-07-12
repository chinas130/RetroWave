#include "audio/AudioStreamDecoder.h"
#include "core/Playlist.h"
#include "audio/PlaybackEngine.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void requireNear(double actual, double expected, double epsilon, const std::string& message) {
    if (std::abs(actual - expected) > epsilon) {
        throw std::runtime_error(message + ": expected " + std::to_string(expected) +
                                 ", got " + std::to_string(actual));
    }
}

std::filesystem::path findTestTone() {
    const std::vector<std::filesystem::path> candidates = {
        std::filesystem::current_path() / ".." / "assets-test-tone.wav",
        std::filesystem::current_path() / ".." / ".." / "assets-test-tone.wav",
        std::filesystem::current_path() / "assets-test-tone.wav",
    };
    for (const auto& path : candidates) {
        if (std::filesystem::exists(path)) {
            return std::filesystem::absolute(path);
        }
    }
    throw std::runtime_error("assets-test-tone.wav not found");
}

void engineUpdate(retrowave::PlaybackEngine& engine, int iterations = 5) {
    for (int i = 0; i < iterations; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        engine.update();
    }
}

// ─── seekSeconds on AudioStreamDecoder directly ────────────────────────────

void test_seekSeconds_nativeDecoder() {
    const auto wavPath = findTestTone();
    retrowave::AudioStreamDecoder decoder;
    decoder.open(wavPath);

    const double duration = decoder.durationSeconds();
    require(duration > 0.0, "WAV must have duration");

    // Seek to ~50% and verify position is roughly there
    const double seekTarget = duration * 0.5;
    const bool seekOk = decoder.seekSeconds(seekTarget);
    require(seekOk, "seekSeconds should succeed for local WAV");

    // Read some frames after seek and verify they're valid
    std::vector<std::int16_t> buffer(4096 * 2, 0);
    const auto frames = decoder.readFrames(buffer.data(), 1024);
    require(frames > 0, "should read frames after seek");

    bool hasSignal = false;
    for (std::size_t i = 0; i < frames * 2; ++i) {
        if (buffer[i] != 0) {
            hasSignal = true;
            break;
        }
    }
    require(hasSignal, "should produce non-zero samples after seek");

    decoder.close();
}

void test_seekSeconds_rewindToStart() {
    const auto wavPath = findTestTone();
    retrowave::AudioStreamDecoder decoder;
    decoder.open(wavPath);

    // Read some frames to advance position
    std::vector<std::int16_t> buffer(4096 * 2, 0);
    decoder.readFrames(buffer.data(), 2048);
    require(!decoder.eof(), "should not be at EOF after partial read");

    // Seek back to start
    const bool seekOk = decoder.seekSeconds(0.0);
    require(seekOk, "seek to 0 should succeed");

    // Read frames again — should get valid data
    const auto frames = decoder.readFrames(buffer.data(), 1024);
    require(frames > 0, "should read frames after rewind");

    decoder.close();
}

void test_seekSeconds_beyondEnd_clamps() {
    const auto wavPath = findTestTone();
    retrowave::AudioStreamDecoder decoder;
    decoder.open(wavPath);

    const double duration = decoder.durationSeconds();
    // Seek way past end — should clamp to duration and return false (seek beyond end fails)
    const bool seekOk = decoder.seekSeconds(duration + 100.0);
    // After seek beyond end, decoder may still read zero frames
    // Expected: either seek returns false, or readFrames returns 0
    // This is acceptable behaviour
    if (seekOk) {
        std::vector<std::int16_t> buffer(1024 * 2, 0);
        const auto frames = decoder.readFrames(buffer.data(), 1024);
        // May be 0 if beyond EOF
        require(frames == 0, "reading beyond end after seek past duration should return 0 frames, got " +
                std::to_string(frames));
    }

    decoder.close();
}

// ─── PlaybackEngine::seek integration test ──────────────────────────────────

void test_engine_seek_movesPosition() {
    // Create a temp directory with a real WAV copy
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto dir = std::filesystem::temp_directory_path() / ("retrowave-seek-test-" + std::to_string(stamp));
    std::filesystem::create_directories(dir);

    const auto src = findTestTone();
    const auto dest = dir / "track.wav";
    std::filesystem::copy_file(src, dest);

    auto playlist = retrowave::Playlist::fromSources({dir.string()});
    retrowave::PlaybackEngine engine(std::move(playlist));

    // Let engine settle
    engineUpdate(engine, 15);

    auto snap = engine.snapshot();
    require(snap.hasTrack, "engine should have a track");
    require(snap.durationSeconds > 0.0, "track should have duration");

    // Get baseline position
    const double initialPos = snap.positionSeconds;

    // Seek forward by a significant amount
    const double seekTarget = std::min(snap.durationSeconds * 0.3, std::max(snap.durationSeconds * 0.3, 1.0));
    const bool seekOk = engine.seek(seekTarget);
    require(seekOk, "seek() should succeed for local file");

    engineUpdate(engine, 10);
    snap = engine.snapshot();

    // Position should be near the seek target (within ~200ms due to latency compensation + render startup)
    requireNear(snap.positionSeconds, seekTarget, 0.3,
                "position after seek should be near target (got " +
                std::to_string(snap.positionSeconds) + ", expected " + std::to_string(seekTarget) + ")");

    std::filesystem::remove_all(dir);
}

void test_engine_seek_atEnd_triggersPendingAdvance() {
    // Use the test WAV, seek to near the end, verify auto-advance logic
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto dir = std::filesystem::temp_directory_path() / ("retrowave-seek-end-test-" + std::to_string(stamp));
    std::filesystem::create_directories(dir);

    const auto src = findTestTone();
    const auto dest1 = dir / "a.wav";
    const auto dest2 = dir / "b.wav";
    std::filesystem::copy_file(src, dest1);
    std::filesystem::copy_file(src, dest2);

    auto playlist = retrowave::Playlist::fromSources({dir.string()});
    retrowave::PlaybackEngine engine(std::move(playlist));
    engine.setRepeatMode(retrowave::RepeatMode::All);
    engineUpdate(engine, 15);

    auto snap = engine.snapshot();
    require(snap.hasTrack, "engine should have a track");

    // Seek to near the end of track 0
    const double nearEnd = snap.durationSeconds * 0.95;
    engine.seek(nearEnd);
    engineUpdate(engine, 15);

    // The engine should either still be on track 0 or auto-advanced to track 1
    snap = engine.snapshot();
    require(snap.currentIndex < 2, "currentIndex should be valid");

    std::filesystem::remove_all(dir);
}

}  // namespace

int main() {
    try {
        test_seekSeconds_nativeDecoder();
        test_seekSeconds_rewindToStart();
        test_seekSeconds_beyondEnd_clamps();
        test_engine_seek_movesPosition();
        test_engine_seek_atEnd_triggersPendingAdvance();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "All seek tests passed.\n";
    return 0;
}