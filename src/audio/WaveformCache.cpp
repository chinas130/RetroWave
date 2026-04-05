#include "audio/WaveformCache.h"

#include "audio/AudioStreamDecoder.h"

#include <algorithm>
#include <cmath>
#include <system_error>

namespace retrowave {
namespace {

constexpr std::size_t kWaveformBins = 160;
constexpr std::size_t kChunkFrames = 1024;
constexpr float kInt16Scale = 32767.0F;

std::vector<float> buildWaveformFromChunks(const std::vector<float>& chunkPeaks) {
    std::vector<float> waveform(kWaveformBins, 0.0F);
    if (chunkPeaks.empty()) {
        return waveform;
    }

    for (std::size_t bin = 0; bin < waveform.size(); ++bin) {
        const std::size_t begin = (bin * chunkPeaks.size()) / waveform.size();
        const std::size_t end = std::max(
            begin + 1,
            ((bin + 1) * chunkPeaks.size()) / waveform.size());

        float peak = 0.0F;
        for (std::size_t index = begin; index < std::min(end, chunkPeaks.size()); ++index) {
            peak = std::max(peak, chunkPeaks[index]);
        }
        waveform[bin] = peak;
    }

    return waveform;
}

}  // namespace

WaveformCache::WaveformCache()
    : worker_([this]() { workerLoop(); }) {}

WaveformCache::~WaveformCache() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wakeup_.notify_one();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void WaveformCache::request(const std::filesystem::path& path) {
    const auto key = makeKey(path);

    {
        std::lock_guard lock(mutex_);
        if (ready_.contains(key) || queued_.contains(key)) {
            return;
        }

        queued_.insert(key);
        jobs_.push_front(key);
    }

    wakeup_.notify_one();
}

std::optional<std::vector<float>> WaveformCache::get(const std::filesystem::path& path) const {
    const auto key = makeKey(path);
    std::lock_guard lock(mutex_);
    const auto it = ready_.find(key);
    if (it == ready_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void WaveformCache::touchReadyKey(const CacheKey& key) {
    auto it = std::find(readyOrder_.begin(), readyOrder_.end(), key);
    if (it != readyOrder_.end()) {
        readyOrder_.erase(it);
    }
    readyOrder_.push_back(key);
}

void WaveformCache::evictIfNeeded() {
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
    auto normalized = std::filesystem::weakly_canonical(path, error);
    if (error) {
        error.clear();
        normalized = std::filesystem::absolute(path, error);
    }
    if (error) {
        normalized = path;
    }

    CacheKey key;
    key.path = normalized.lexically_normal().string();

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

std::vector<float> WaveformCache::buildWaveform(const std::filesystem::path& path) {
    AudioStreamDecoder decoder;
    decoder.open(path);

    std::vector<std::int16_t> buffer(kChunkFrames * static_cast<std::size_t>(decoder.channels()), 0);
    std::vector<float> chunkPeaks;

    while (!decoder.eof()) {
        const auto framesRead = decoder.readFrames(buffer.data(), kChunkFrames);
        if (framesRead == 0) {
            break;
        }

        float peak = 0.0F;
        for (std::size_t frame = 0; frame < framesRead; ++frame) {
            for (int channel = 0; channel < decoder.channels(); ++channel) {
                const auto sampleIndex =
                    frame * static_cast<std::size_t>(decoder.channels()) + static_cast<std::size_t>(channel);
                const float sample = static_cast<float>(buffer[sampleIndex]) / kInt16Scale;
                peak = std::max(peak, std::abs(sample));
            }
        }
        chunkPeaks.push_back(peak);
    }

    return buildWaveformFromChunks(chunkPeaks);
}

void WaveformCache::workerLoop() {
    while (true) {
        CacheKey job;

        {
            std::unique_lock lock(mutex_);
            wakeup_.wait(lock, [this]() { return stop_ || !jobs_.empty(); });
            if (stop_ && jobs_.empty()) {
                return;
            }

            job = jobs_.front();
            jobs_.pop_front();
        }

        std::vector<float> waveform;
        try {
            waveform = buildWaveform(job.path);
        } catch (...) {
            waveform.clear();
        }

        std::lock_guard lock(mutex_);
        queued_.erase(job);
        if (!waveform.empty()) {
            ready_[job] = std::move(waveform);
            touchReadyKey(job);
            evictIfNeeded();
        }
    }
}

}  // namespace retrowave
