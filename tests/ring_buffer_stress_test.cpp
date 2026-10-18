// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <numeric>
#include <limits>
#include <random>
#include <algorithm>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wkeyword-macro"
#define private public
#include "audio/AudioRingBuffer.h"
#undef private
#pragma GCC diagnostic pop

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testWrapAround() {
    retrowave::AudioRingBuffer buffer(4096, 2);
    // capacity is frames, here 4096. 4096 is power of 2, so mask is 4095.
    // Set near overflow.
    std::size_t nearMax = std::numeric_limits<std::size_t>::max() - 2000;
    buffer.writePos_.store(nearMax);
    buffer.readPos_.store(nearMax);
    
    std::vector<std::int16_t> src(1000 * 2, 42); // 1000 frames
    std::vector<std::int16_t> dst(1000 * 2, 0);

    for (int i = 0; i < 10; ++i) { // 10 iterations will push writePos over max
        auto pushed = buffer.push(src.data(), 1000);
        require(pushed == 1000, "Should push 1000 frames");
        
        auto popped = buffer.pop(dst.data(), 1000);
        require(popped == 1000, "Should pop 1000 frames");
        
        for(int j=0; j<2000; ++j) {
            require(dst[j] == 42, "Data corrupted across wrap-around");
        }
    }
}

void testSpscMultiThreadTorture() {
    retrowave::AudioRingBuffer buffer(8192, 1);
    constexpr std::size_t kTotalFrames = 50'000'000;

    std::thread writer([&]() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<std::size_t> dist(1, 4096);
        std::size_t framesWritten = 0;
        
        std::vector<std::int16_t> chunk(4096);
        while (framesWritten < kTotalFrames) {
            std::size_t toWrite = std::min(dist(gen), kTotalFrames - framesWritten);
            for (std::size_t i = 0; i < toWrite; ++i) {
                chunk[i] = static_cast<std::int16_t>((framesWritten + i) & 0x7FFF);
            }
            std::size_t pushed = buffer.push(chunk.data(), toWrite);
            framesWritten += pushed;
            if (pushed < toWrite) {
                std::this_thread::yield();
            }
        }
    });

    std::thread reader([&]() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<std::size_t> dist(1, 4096);
        std::size_t framesRead = 0;
        
        std::vector<std::int16_t> chunk(4096);
        while (framesRead < kTotalFrames) {
            std::size_t toRead = std::min(dist(gen), kTotalFrames - framesRead);
            std::size_t popped = buffer.pop(chunk.data(), toRead);
            for (std::size_t i = 0; i < popped; ++i) {
                std::int16_t expected = static_cast<std::int16_t>((framesRead + i) & 0x7FFF);
                require(chunk[i] == expected, "Torture test failed: corrupted sample!");
            }
            framesRead += popped;
            if (popped == 0) {
                std::this_thread::yield();
            }
        }
    });

    writer.join();
    reader.join();
}

void testResetUnderConcurrency() {
    retrowave::AudioRingBuffer buffer(4096, 2);
    
    std::atomic<bool> running{true};
    std::thread reader([&]() {
        std::vector<std::int16_t> chunk(1024);
        while (running.load()) {
            (void)buffer.pop(chunk.data(), 512);
        }
    });

    for (int i = 0; i < 10000; ++i) {
        buffer.flush();
        require(buffer.availableFrames() == 0, "Buffer should be empty after reset");
        require(buffer.freeFrames() == 4096, "Buffer should be fully free after reset");
    }
    
    running.store(false);
    reader.join();
}

} // namespace

int main() {
    try {
        testWrapAround();
        testSpscMultiThreadTorture();
        testResetUnderConcurrency();
        std::cout << "Ring buffer stress test passed.\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
