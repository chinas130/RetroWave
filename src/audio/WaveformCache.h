#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace retrowave {

class WaveformCache {
  public:
    WaveformCache();
    ~WaveformCache();

    WaveformCache(const WaveformCache&) = delete;
    WaveformCache& operator=(const WaveformCache&) = delete;

    void request(const std::filesystem::path& path);
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
    [[nodiscard]] static std::vector<float> buildWaveform(const std::filesystem::path& path);
    void touchReadyKey(const CacheKey& key);
    void evictIfNeeded();

    void workerLoop();

    static constexpr std::size_t maxReadyEntries_ = 24;
    mutable std::mutex mutex_;
    mutable std::condition_variable wakeup_;
    std::unordered_map<CacheKey, std::vector<float>, CacheKeyHash> ready_;
    std::deque<CacheKey> readyOrder_;
    std::unordered_set<CacheKey, CacheKeyHash> queued_;
    std::deque<CacheKey> jobs_;
    std::thread worker_;
    bool stop_ = false;
};

}  // namespace retrowave
