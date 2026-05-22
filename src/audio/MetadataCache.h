#pragma once

#include "audio/AudioDecoder.h"

#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace retrowave {

class MetadataCache {
  public:
    [[nodiscard]] std::optional<TrackMetadata> get(const std::filesystem::path& path) const;
    void store(const std::filesystem::path& path, TrackMetadata metadata);

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
    void touchReadyKey(const CacheKey& key);
    void evictIfNeeded();

    static constexpr std::size_t maxReadyEntries_ = 6;
    mutable std::mutex mutex_;
    std::unordered_map<CacheKey, TrackMetadata, CacheKeyHash> ready_;
    std::deque<CacheKey> readyOrder_;
};

}  // namespace retrowave
