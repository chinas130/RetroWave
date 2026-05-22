#include "audio/MetadataCache.h"

#include <algorithm>
#include <system_error>

namespace retrowave {

std::optional<TrackMetadata> MetadataCache::get(const std::filesystem::path& path) const {
    const auto key = makeKey(path);
    std::lock_guard lock(mutex_);
    const auto it = ready_.find(key);
    if (it == ready_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void MetadataCache::store(const std::filesystem::path& path, TrackMetadata metadata) {
    const auto key = makeKey(path);
    std::lock_guard lock(mutex_);
    ready_[key] = std::move(metadata);
    touchReadyKey(key);
    evictIfNeeded();
}

void MetadataCache::touchReadyKey(const CacheKey& key) {
    auto it = std::find(readyOrder_.begin(), readyOrder_.end(), key);
    if (it != readyOrder_.end()) {
        readyOrder_.erase(it);
    }
    readyOrder_.push_back(key);
}

void MetadataCache::evictIfNeeded() {
    while (ready_.size() > maxReadyEntries_ && !readyOrder_.empty()) {
        const CacheKey evicted = readyOrder_.front();
        readyOrder_.pop_front();
        ready_.erase(evicted);
    }
}

std::size_t MetadataCache::CacheKeyHash::operator()(const CacheKey& key) const noexcept {
    std::size_t hash = std::hash<std::string>{}(key.path);
    hash ^= std::hash<std::uintmax_t>{}(key.fileSize) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
    hash ^= std::hash<std::int64_t>{}(key.writeTick) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
    return hash;
}

MetadataCache::CacheKey MetadataCache::makeKey(const std::filesystem::path& path) {
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
