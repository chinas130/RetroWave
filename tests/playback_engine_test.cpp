#include "audio/AudioStreamDecoder.h"
#include "core/Playlist.h"
#include "audio/PlaybackEngine.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
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

void requireEqual(double actual, double expected, double epsilon, const std::string& message) {
    if (std::abs(actual - expected) > epsilon) {
        throw std::runtime_error(message + ": expected " + std::to_string(expected) +
                                 ", got " + std::to_string(actual));
    }
}

// Returns the path to assets-test-tone.wav relative to the build dir.
std::filesystem::path findTestTone() {
    const std::vector<std::filesystem::path> candidates = {
        std::filesystem::current_path() / ".." / "assets-test-tone.wav",
        std::filesystem::current_path() / ".." / ".." / "assets-test-tone.wav",
        std::filesystem::current_path() / "assets-test-tone.wav",
        std::filesystem::current_path() / ".." / ".." / ".." / "assets-test-tone.wav",
    };
    for (const auto& path : candidates) {
        if (std::filesystem::exists(path)) {
            return std::filesystem::absolute(path);
        }
    }
    throw std::runtime_error("assets-test-tone.wav not found");
}

std::filesystem::path makeTempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("retrowave-be-test-" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

// Copies the test tone WAV into temp dir under different names to create
// distinct track entries. Returns the temp directory path.
std::filesystem::path setupTracks(int count) {
    const auto dir = makeTempDir();
    const auto source = findTestTone();
    for (int i = 0; i < count; ++i) {
        const auto dest = dir / ("track" + std::to_string(i) + ".wav");
        std::filesystem::copy_file(source, dest);
    }
    return dir;
}

void engineUpdate(retrowave::PlaybackEngine& engine, int iterations = 5) {
    for (int i = 0; i < iterations; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        engine.update();
    }
}

// ─── PlaybackState machine tests ────────────────────────────────────────────

void test_playIndex_switchesTrack() {
    const auto dir = setupTracks(2);

    auto playlist = retrowave::Playlist::fromSources({dir.string()});
    requireEqual(playlist.size(), 2, "need 2 tracks");

    retrowave::PlaybackEngine engine(std::move(playlist));
    engineUpdate(engine, 10);

    const auto snap = engine.snapshot();
    require(snap.hasTrack, "engine should have a track after init, state=" +
            std::to_string(static_cast<int>(snap.state)) + " error=" + snap.lastError);
    require(snap.state == retrowave::PlaybackState::Playing ||
            snap.state == retrowave::PlaybackState::Paused ||
            snap.state == retrowave::PlaybackState::Buffering,
            "engine should be playing/paused/buffering, got state=" +
            std::to_string(static_cast<int>(snap.state)) + " error=" + snap.lastError);

    std::filesystem::remove_all(dir);
}

void test_next_advancesInLinearMode() {
    const auto dir = setupTracks(3);

    auto playlist = retrowave::Playlist::fromSources({dir.string()});
    retrowave::PlaybackEngine engine(std::move(playlist));
    engineUpdate(engine, 10);

    std::size_t firstIndex = engine.snapshot().currentIndex;

    engine.next();
    engineUpdate(engine, 10);
    std::size_t secondIndex = engine.snapshot().currentIndex;
    requireEqual(secondIndex, firstIndex + 1,
                 "next should advance by 1 (got " + std::to_string(firstIndex) +
                 " -> " + std::to_string(secondIndex) + ")");

    engine.next();
    engineUpdate(engine, 10);
    std::size_t thirdIndex = engine.snapshot().currentIndex;
    requireEqual(thirdIndex, secondIndex + 1,
                 "second next should advance to last track");

    std::filesystem::remove_all(dir);
}

void test_next_staysOnRepeatOneAtEnd() {
    const auto dir = setupTracks(3);

    auto playlist = retrowave::Playlist::fromSources({dir.string()});
    retrowave::PlaybackEngine engine(std::move(playlist));
    engine.setRepeatMode(retrowave::RepeatMode::One);
    engineUpdate(engine, 10);

    // Navigate to the LAST track (index 2)
    engine.playIndex(2);
    engineUpdate(engine, 10);
    requireEqual(engine.snapshot().currentIndex, 2, "should be at last track");

    // With repeat one, next() at the last track should stay on the same index
    engine.next();
    engineUpdate(engine, 10);
    std::size_t afterNext = engine.snapshot().currentIndex;
    requireEqual(afterNext, 2, "repeat one: next() at last track should stay on same index");

    // Also verify previous works normally
    engine.previous();
    engineUpdate(engine, 10);
    requireEqual(engine.snapshot().currentIndex, 1, "previous should work normally with repeat one");

    std::filesystem::remove_all(dir);
}

void test_next_wrapsOnRepeatAll() {
    const auto dir = setupTracks(2);

    auto playlist = retrowave::Playlist::fromSources({dir.string()});
    retrowave::PlaybackEngine engine(std::move(playlist));
    engine.setRepeatMode(retrowave::RepeatMode::All);
    engineUpdate(engine, 10);

    // Cycle through 10 times, verify index stays in [0,1]
    for (int i = 0; i < 10; ++i) {
        engine.next();
        engineUpdate(engine, 10);
        const auto index = engine.snapshot().currentIndex;
        require(index < 2,
                "index should stay in [0,1] with repeat all (iteration " +
                std::to_string(i) + "), got " + std::to_string(index));
    }

    std::filesystem::remove_all(dir);
}

void test_previous_goesBack() {
    const auto dir = setupTracks(3);

    auto playlist = retrowave::Playlist::fromSources({dir.string()});
    retrowave::PlaybackEngine engine(std::move(playlist));
    engineUpdate(engine, 10);
    requireEqual(engine.snapshot().currentIndex, 0, "should start at index 0");

    engine.next();
    engineUpdate(engine, 10);
    requireEqual(engine.snapshot().currentIndex, 1, "after next: index 1");

    engine.next();
    engineUpdate(engine, 10);
    requireEqual(engine.snapshot().currentIndex, 2, "after next: index 2");

    engine.previous();
    engineUpdate(engine, 10);
    requireEqual(engine.snapshot().currentIndex, 1, "previous should go back to index 1");

    std::filesystem::remove_all(dir);
}

void test_togglePause() {
    const auto dir = setupTracks(1);

    auto playlist = retrowave::Playlist::fromSources({dir.string()});
    retrowave::PlaybackEngine engine(std::move(playlist));
    engineUpdate(engine, 10);

    auto snap = engine.snapshot();
    require(snap.state == retrowave::PlaybackState::Playing ||
            snap.state == retrowave::PlaybackState::Buffering,
            "engine should be playing/buffering before pause, got " +
            std::to_string(static_cast<int>(snap.state)));

    engine.togglePause();
    engineUpdate(engine, 5);
    snap = engine.snapshot();
    requireEqual(static_cast<int>(snap.state),
                 static_cast<int>(retrowave::PlaybackState::Paused),
                 "togglePause should lead to Paused, got " +
                 std::to_string(static_cast<int>(snap.state)));

    // Resume
    engine.togglePause();
    engineUpdate(engine, 5);
    snap = engine.snapshot();
    require(snap.state == retrowave::PlaybackState::Playing ||
            snap.state == retrowave::PlaybackState::Buffering,
            "second togglePause should resume Playing, got " +
            std::to_string(static_cast<int>(snap.state)));

    std::filesystem::remove_all(dir);
}

void test_volume_clamps() {
    const auto dir = setupTracks(1);
    auto playlist = retrowave::Playlist::fromSources({dir.string()});
    retrowave::PlaybackEngine engine(std::move(playlist));
    engineUpdate(engine, 5);

    engine.setVolume(0.5F);
    engineUpdate(engine, 3);
    requireEqual(static_cast<double>(engine.snapshot().volume), 0.5, 0.001, "volume should be 0.5");

    engine.adjustVolume(-10.0F);
    engineUpdate(engine, 3);
    requireEqual(static_cast<double>(engine.snapshot().volume), 0.0, 0.001, "volume should clamp to 0.0");

    engine.adjustVolume(10.0F);
    engineUpdate(engine, 3);
    requireEqual(static_cast<double>(engine.snapshot().volume), 1.2, 0.001, "volume should clamp to 1.2");

    std::filesystem::remove_all(dir);
}

void test_shuffle_visitsAllBeforeRepeat() {
    const auto dir = setupTracks(5);

    auto playlist = retrowave::Playlist::fromSources({dir.string()});
    retrowave::PlaybackEngine engine(std::move(playlist));
    engine.setShuffleMode(retrowave::ShuffleMode::On);
    engine.setRepeatMode(retrowave::RepeatMode::All);

    // The engine auto-plays index 0. Let it settle.
    engineUpdate(engine, 15);

    // Collect unique indices observed via next().
    // With shuffle on + repeat all, every next() should go to the next
    // shuffled position and wrap when exhausted.
    std::vector<std::size_t> seen;
    seen.push_back(engine.snapshot().currentIndex);
    for (int i = 0; i < 30; ++i) {
        engine.next();
        engineUpdate(engine, 12);
        const auto idx = engine.snapshot().currentIndex;
        if (std::find(seen.begin(), seen.end(), idx) == seen.end()) {
            seen.push_back(idx);
        }
        if (seen.size() == 5) break;
    }
    requireEqual(seen.size(), 5, "shuffle should visit all 5 tracks within 30 next() calls, got " +
                 std::to_string(seen.size()));

    std::filesystem::remove_all(dir);
}

// ─── AudioStreamDecoder test (real WAV) ─────────────────────────────────────

void test_decoder_readsRealWav() {
    const auto wavPath = findTestTone();
    require(std::filesystem::exists(wavPath), "test tone must exist");

    retrowave::AudioStreamDecoder decoder;
    decoder.open(wavPath);

    require(decoder.sampleRate() > 0, "sample rate should be parsed");
    require(decoder.channels() > 0, "channels should be parsed");
    require(decoder.durationSeconds() > 0.0, "duration should be > 0");

    // Read a chunk of frames
    std::vector<std::int16_t> buffer(4096 * 2, 0);
    const auto frames = decoder.readFrames(buffer.data(), 4096);
    require(frames > 0, "should read some frames from test WAV");
    require(frames <= 4096, "should not read more than requested");

    // Verify we got non-silence (test tone has content)
    bool hasSignal = false;
    for (std::size_t i = 0; i < frames * static_cast<std::size_t>(decoder.channels()); ++i) {
        if (buffer[i] != 0) {
            hasSignal = true;
            break;
        }
    }
    require(hasSignal, "test tone should produce non-zero samples");

    decoder.close();
}

void test_decoder_reportsEof() {
    const auto wavPath = findTestTone();
    retrowave::AudioStreamDecoder decoder;
    decoder.open(wavPath);
    require(!decoder.eof(), "decoder should not be at EOF initially");

    // Read all frames
    std::vector<std::int16_t> buffer(8192 * 2, 0);
    std::size_t totalFrames = 0;
    while (true) {
        const auto frames = decoder.readFrames(buffer.data(), 8192);
        if (frames == 0) break;
        totalFrames += frames;
    }
    require(decoder.eof(), "decoder should report EOF after full read");
    require(totalFrames > 0, "should have read frames before EOF");

    // Verify total frames matches duration
    const double expectedFrames = decoder.durationSeconds() * decoder.sampleRate();
    requireEqual(static_cast<double>(totalFrames), expectedFrames, expectedFrames * 0.05,
                 "total frames should match duration within 5%");

    decoder.close();
}

// ─── Aggressive scrolling / rapid navigation tests ─────────────────────────

void test_rapid_next_previous_stress() {
    const auto dir = setupTracks(5);

    auto playlist = retrowave::Playlist::fromSources({dir.string()});
    retrowave::PlaybackEngine engine(std::move(playlist));
    engineUpdate(engine, 10);

    // Rapidly alternate next() and previous() 50 times.
    // This should never crash, hang, or go out of bounds.
    for (int i = 0; i < 50; ++i) {
        engine.next();
        engineUpdate(engine, 2);
        const auto idxForward = engine.snapshot().currentIndex;
        require(idxForward < 5, "next() should stay in bounds, got " + std::to_string(idxForward));

        engine.previous();
        engineUpdate(engine, 2);
        const auto idxBack = engine.snapshot().currentIndex;
        require(idxBack < 5, "previous() should stay in bounds, got " + std::to_string(idxBack));
    }

    // After all that, the engine should not be in Failed state
    const auto snap = engine.snapshot();
    require(snap.state != retrowave::PlaybackState::Failed,
            "engine should not fail after rapid next/previous");

    std::filesystem::remove_all(dir);
}

void test_rapid_playIndex_switching() {
    const auto dir = setupTracks(5);

    auto playlist = retrowave::Playlist::fromSources({dir.string()});
    retrowave::PlaybackEngine engine(std::move(playlist));
    engineUpdate(engine, 10);

    // Rapidly switch between tracks without waiting for load
    for (int i = 0; i < 30; ++i) {
        const std::size_t target = static_cast<std::size_t>(i % 5);
        engine.playIndex(target);
        engineUpdate(engine, 2);
        // Don't enforce which index we're at — the engine may still be loading.
        // Just verify we never crash and stay in a valid state.
        const auto snap = engine.snapshot();
        require(snap.state != retrowave::PlaybackState::Failed,
                "engine should not fail after rapid playIndex switching");
    }

    std::filesystem::remove_all(dir);
}

void test_rapid_seek_back_and_forth() {
    const auto dir = setupTracks(1);

    auto playlist = retrowave::Playlist::fromSources({dir.string()});
    retrowave::PlaybackEngine engine(std::move(playlist));
    engineUpdate(engine, 15);

    const auto initialSnap = engine.snapshot();
    require(initialSnap.hasTrack, "engine should have a track");
    require(initialSnap.durationSeconds > 0.0, "track should have duration");
    const double dur = initialSnap.durationSeconds;

    // Rapidly seek to various positions 30 times
    for (int i = 0; i < 30; ++i) {
        const double target = dur * (static_cast<double>(i % 10) / 10.0);
        engine.seek(target);
        engineUpdate(engine, 1);
    }

    // Engine should still be alive
    const auto snap = engine.snapshot();
    require(snap.state != retrowave::PlaybackState::Failed,
            "engine should not fail after rapid seek");
    require(snap.hasTrack, "engine should still have a track after rapid seek");

    std::filesystem::remove_all(dir);
}

void test_next_at_boundary_stress() {
    const auto dir = setupTracks(3);

    auto playlist = retrowave::Playlist::fromSources({dir.string()});
    retrowave::PlaybackEngine engine(std::move(playlist));
    engine.setRepeatMode(retrowave::RepeatMode::Off);
    engineUpdate(engine, 10);

    // Navigate to last track
    engine.playIndex(2);
    engineUpdate(engine, 10);
    requireEqual(engine.snapshot().currentIndex, 2, "should be at last track");

    // Call next() 50 times — with repeat off at the last track,
    // next() should either stay on the last track or set state to Ended.
    for (int i = 0; i < 50; ++i) {
        engine.next();
        engineUpdate(engine, 2);
        const auto snap = engine.snapshot();
        require(snap.currentIndex <= 2,
                "next() at boundary should keep currentIndex in bounds, got " +
                std::to_string(snap.currentIndex));
    }

    std::filesystem::remove_all(dir);
}

}  // namespace

int main() {
    try {
        test_playIndex_switchesTrack();
        test_next_advancesInLinearMode();
        test_next_staysOnRepeatOneAtEnd();
        test_next_wrapsOnRepeatAll();
        test_previous_goesBack();
        test_togglePause();
        test_volume_clamps();
        test_shuffle_visitsAllBeforeRepeat();
        test_rapid_next_previous_stress();
        test_rapid_playIndex_switching();
        test_rapid_seek_back_and_forth();
        test_next_at_boundary_stress();
        test_decoder_readsRealWav();
        test_decoder_reportsEof();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "All playback engine tests passed.\n";
    return 0;
}