// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace retrowave {

inline constexpr std::size_t kWaveformBinCount = 160;
inline constexpr std::uint32_t kWaveformMagic = 0x46565752;
inline constexpr std::uint16_t kWaveformVersion = 1;
inline constexpr std::size_t kExpectedFileSize =
    sizeof(std::uint32_t) + sizeof(std::uint16_t) + sizeof(std::uint16_t) + (kWaveformBinCount * sizeof(float));
static_assert(kExpectedFileSize == 648, "kExpectedFileSize must be exactly 648 bytes");

class WaveformCache {
  public:
    WaveformCache();
    void store(const std::filesystem::path& path, std::vector<float> waveform);
    [[nodiscard]] std::optional<std::vector<float>> get(const std::filesystem::path& path) const;

  private:
    struct CacheKey {
        std::string path;
        std::uintmax_t fileSize = 0;
        std::int64_t writeTick = 0;

        [[nodiscard]] bool operator==(const CacheKey& other) const noexcept {
            return path == other.path && fileSize == other.fileSize && writeTick == other.writeTick;
        }
    };

    struct CacheKeyHash {
        [[nodiscard]] std::size_t operator()(const CacheKey& key) const noexcept;
    };

    [[nodiscard]] static CacheKey makeKey(const std::filesystem::path& path);
    void touchReadyKey(const CacheKey& key) const;
    void evictIfNeeded() const;

    [[nodiscard]] std::filesystem::path getDiskCacheDir() const;
    [[nodiscard]] std::string getHashString(const CacheKey& key) const;

    static constexpr std::size_t maxReadyEntries_ = 128;
    mutable std::mutex mutex_;
    mutable std::unordered_map<CacheKey, std::vector<float>, CacheKeyHash> ready_;
    mutable std::deque<CacheKey> readyOrder_;
    std::filesystem::path cacheDir_;
};

}  // namespace retrowave
