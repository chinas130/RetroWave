// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#include "audio/AudioStreamDecoder.h"
#include "audio/PlaybackEngine.h"
#include "core/Playlist.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <random>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::filesystem::path makeTempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("retrowave-audio-test-" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

void writeBinary(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
}

void testBadFiles() {
    auto dir = makeTempDir();
    auto emptyFile = dir / "empty.mp3";
    writeBinary(emptyFile, {});

    auto garbageFile = dir / "garbage.mp3";
    std::vector<uint8_t> garbage(16, 0x42);
    writeBinary(garbageFile, garbage);
    
    auto txtFile = dir / "fake.mp3";
    writeBinary(txtFile, { 'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd' });

    retrowave::Playlist playlist = retrowave::Playlist::fromSources({
        emptyFile.string(),
        garbageFile.string(),
        txtFile.string()
    });

    retrowave::PlaybackEngine engine(playlist);

    for (std::size_t i = 0; i < 3; ++i) {
        engine.playIndex(i);
        // Wait for it to fail
        bool failed = false;
        for (int j = 0; j < 50; ++j) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            engine.update();
            auto snap = engine.snapshot();
            if (snap.state == retrowave::PlaybackState::Failed) {
                failed = true;
                require(!snap.lastError.empty(), "Should have an error message");
                break;
            }
        }
        require(failed, "Engine should reach Failed state on bad file: " + std::to_string(i));
    }

    std::filesystem::remove_all(dir);
}

void testSeekBoundaries() {
    std::filesystem::path testFile = "../assets-test-tone.wav";
    require(std::filesystem::exists(testFile), "../assets-test-tone.wav must exist");
    retrowave::Playlist playlist = retrowave::Playlist::fromSources({testFile.string()});
    
    retrowave::PlaybackEngine engine(playlist);
    engine.playIndex(0);
    
    // wait until playing
    double duration = 0;
    while (true) {
        engine.update();
        auto snap = engine.snapshot();
        if (snap.state == retrowave::PlaybackState::Playing) {
            duration = snap.durationSeconds;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    // Seek negative
    engine.seek(-10.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    engine.update();
    require(engine.snapshot().positionSeconds >= 0.0, "Position should clamp or be valid");

    // Seek exactly to end
    engine.seek(duration);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    engine.update();

    // Seek beyond end
    engine.seek(duration + 60.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    engine.update();
    
    auto snap = engine.snapshot();
    require(snap.state == retrowave::PlaybackState::Ended || snap.state == retrowave::PlaybackState::Playing || snap.state == retrowave::PlaybackState::Idle, 
            "Should gracefully handle seek beyond end");
}

void testExtremeThrashing() {
    retrowave::Playlist playlist = retrowave::Playlist::fromSources({"../assets-test-tone.wav"});
    retrowave::PlaybackEngine engine(playlist);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<double> seekDist(-10.0, 100.0);
    
    for (int i = 0; i < 200; ++i) {
        engine.playIndex(0);
        engine.seek(seekDist(gen));
        engine.update();
    }
    
    // Allow engine to settle
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    engine.update();
}

} // namespace

int main() {
    try {
        testBadFiles();
        testSeekBoundaries();
        testExtremeThrashing();
        std::cout << "Audio pipeline edge test passed.\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
