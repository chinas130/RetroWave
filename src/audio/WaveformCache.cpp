// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#include "audio/WaveformCache.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace retrowave {

WaveformCache::WaveformCache() {
    std::error_code ec;
    cacheDir_ = getDiskCacheDir();
    std::filesystem::create_directories(cacheDir_, ec);
}

std::filesystem::path WaveformCache::getDiskCacheDir() const {
    const char* xdgCacheHome = std::getenv("XDG_CACHE_HOME");
    if (xdgCacheHome != nullptr && xdgCacheHome[0] != '\0') {
        return std::filesystem::path(xdgCacheHome) / "retrowave" / "waveforms";
    }

    const char* home = std::getenv("HOME");
    if (home == nullptr) {
        return std::filesystem::temp_directory_path() / "retrowave" / "waveforms";
    }

#ifdef __APPLE__
    return std::filesystem::path(home) / "Library" / "Caches" / "retrowave" / "waveforms";
#else
    return std::filesystem::path(home) / ".cache" / "retrowave" / "waveforms";
#endif
}

std::string WaveformCache::getHashString(const CacheKey& key) const {
    const std::size_t hashVal = CacheKeyHash{}(key);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << hashVal;
    return oss.str();
}

void WaveformCache::store(const std::filesystem::path& path, std::vector<float> waveform) {
    if (waveform.empty() || waveform.size() != kWaveformBinCount) {
        return;
    }

    const auto key = makeKey(path);

    {
        std::lock_guard lock(mutex_);
        ready_[key] = waveform;
        touchReadyKey(key);
        evictIfNeeded();
    }

    const std::string hashHex = getHashString(key);
    const std::filesystem::path tmpPath = cacheDir_ / (hashHex + ".tmp");
    const std::filesystem::path binPath = cacheDir_ / (hashHex + ".bin");

    std::ofstream ofs(tmpPath, std::ios::binary);
    if (!ofs) {
        return;
    }

    constexpr std::uint32_t magic = kWaveformMagic;
    constexpr std::uint16_t version = kWaveformVersion;
    constexpr auto binCount = static_cast<std::uint16_t>(kWaveformBinCount);

    ofs.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    ofs.write(reinterpret_cast<const char*>(&version), sizeof(version));
    ofs.write(reinterpret_cast<const char*>(&binCount), sizeof(binCount));
    ofs.write(reinterpret_cast<const char*>(waveform.data()), static_cast<std::streamsize>(waveform.size() * sizeof(float)));

    std::error_code ec;
    if (!ofs) {
        ofs.close();
        std::filesystem::remove(tmpPath, ec);
        return;
    }

    ofs.close();
    std::filesystem::rename(tmpPath, binPath, ec);
}

std::optional<std::vector<float>> WaveformCache::get(const std::filesystem::path& path) const {
    const auto key = makeKey(path);

    {
        std::lock_guard lock(mutex_);
        const auto it = ready_.find(key);
        if (it != ready_.end()) {
            touchReadyKey(key);
            return it->second;
        }
    }

    const std::string hashHex = getHashString(key);
    const std::filesystem::path binPath = cacheDir_ / (hashHex + ".bin");

    std::error_code ec;
    if (!std::filesystem::is_regular_file(binPath, ec) || ec) {
        return std::nullopt;
    }

    const auto fileSize = std::filesystem::file_size(binPath, ec);
    if (ec || fileSize != kExpectedFileSize) {
        ec.clear();
        std::filesystem::remove(binPath, ec);
        return std::nullopt;
    }

    std::ifstream ifs(binPath, std::ios::binary);
    if (!ifs) {
        return std::nullopt;
    }

    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t binCount = 0;

    ifs.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    ifs.read(reinterpret_cast<char*>(&version), sizeof(version));
    ifs.read(reinterpret_cast<char*>(&binCount), sizeof(binCount));

    if (!ifs || magic != kWaveformMagic || version != kWaveformVersion || binCount != kWaveformBinCount) {
        ifs.close();
        std::filesystem::remove(binPath, ec);
        return std::nullopt;
    }

    std::vector<float> waveform(kWaveformBinCount);
    ifs.read(reinterpret_cast<char*>(waveform.data()), static_cast<std::streamsize>(kWaveformBinCount * sizeof(float)));

    if (!ifs) {
        ifs.close();
        std::filesystem::remove(binPath, ec);
        return std::nullopt;
    }

    for (const float sample : waveform) {
        if (!std::isfinite(sample)) {
            ifs.close();
            std::filesystem::remove(binPath, ec);
            return std::nullopt;
        }
    }

    ifs.close();

    {
        std::lock_guard lock(mutex_);
        ready_[key] = waveform;
        touchReadyKey(key);
        evictIfNeeded();
    }

    return waveform;
}

void WaveformCache::touchReadyKey(const CacheKey& key) const {
    auto it = std::find(readyOrder_.begin(), readyOrder_.end(), key);
    if (it != readyOrder_.end()) {
        readyOrder_.erase(it);
    }
    readyOrder_.push_back(key);
}

void WaveformCache::evictIfNeeded() const {
    while (ready_.size() > maxReadyEntries_ && !readyOrder_.empty()) {
        const CacheKey evicted = readyOrder_.front();
        readyOrder_.pop_front();
        ready_.erase(evicted);
    }
}

std::size_t WaveformCache::CacheKeyHash::operator()(const CacheKey& key) const noexcept {
    std::size_t hash = std::hash<std::string>{}(key.path);
    hash ^= std::hash<std::uintmax_t>{}(key.fileSize) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
    hash ^= std::hash<std::int64_t>{}(key.writeTick) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
    return hash;
}

WaveformCache::CacheKey WaveformCache::makeKey(const std::filesystem::path& path) {
    std::error_code error;
    auto normalized = path.lexically_normal();
    if (normalized.is_relative()) {
        normalized = std::filesystem::absolute(normalized, error);
        if (error) {
            normalized = path.lexically_normal();
        }
    }

    CacheKey key;
    key.path = normalized.string();

    error.clear();
    key.fileSize = std::filesystem::file_size(normalized, error);
    if (error) {
        key.fileSize = 0;
    }

    error.clear();
    const auto writeTime = std::filesystem::last_write_time(normalized, error);
    if (!error) {
        key.writeTick = static_cast<std::int64_t>(writeTime.time_since_epoch().count());
    }

    return key;
}

}  // namespace retrowave
