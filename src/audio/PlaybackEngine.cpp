#include "audio/PlaybackEngine.h"

#include "audio/AudioOutput.h"
#include "audio/AudioStreamDecoder.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

namespace retrowave {
namespace {

constexpr float kInt16Scale = 32767.0F;
constexpr std::size_t kStreamBufferFrames = static_cast<std::size_t>(AudioOutput::sampleRate()) / 2;
constexpr std::size_t kDecoderChunkFrames = 2048;
constexpr auto kDecoderSleep = std::chrono::milliseconds(12);

float clamp(float value, float minimum, float maximum) {
    return std::max(minimum, std::min(maximum, value));
}

std::int16_t applyGain(std::int16_t sample, float gain) {
    const float scaled = static_cast<float>(sample) * gain;
    const float clamped = clamp(scaled, -kInt16Scale, kInt16Scale);
    return static_cast<std::int16_t>(std::lrint(clamped));
}

std::array<float, kPlaybackVisualizerBins> buildVisualizerWindow(
    const std::int16_t* samples,
    std::size_t frameCount,
    int channels) {
    std::array<float, kPlaybackVisualizerBins> bins{};
    if (samples == nullptr || frameCount == 0 || channels <= 0) {
        return bins;
    }

    const auto framesPerBin = std::max<std::size_t>(1, frameCount / kPlaybackVisualizerBins);

    for (std::size_t bin = 0; bin < kPlaybackVisualizerBins; ++bin) {
        const auto frameStart = bin * framesPerBin;
        if (frameStart >= frameCount) {
            break;
        }

        const auto frameEnd = std::min(frameCount, frameStart + framesPerBin);
        float energy = 0.0F;
        std::size_t sampleCount = 0;

        for (std::size_t frame = frameStart; frame < frameEnd; ++frame) {
            for (int channel = 0; channel < channels; ++channel) {
                const auto sampleIndex =
                    frame * static_cast<std::size_t>(channels) + static_cast<std::size_t>(channel);
                const float sample = static_cast<float>(samples[sampleIndex]) / kInt16Scale;
                energy += sample * sample;
                ++sampleCount;
            }
        }

        if (sampleCount > 0) {
            bins[bin] = std::sqrt(energy / static_cast<float>(sampleCount));
        }
    }

    return bins;
}

}  // namespace

PlaybackEngine::PlaybackEngine(Playlist playlist)
    : playlist_(std::move(playlist)),
      ringBuffer_(kStreamBufferFrames, 2) {
    try {
        output_ = std::make_unique<AudioOutput>(
            [this](std::int16_t* destination, std::size_t frames) { return renderFrames(destination, frames); });
        output_->start();
    } catch (const std::exception& error) {
        audioError_ = error.what();
        output_.reset();
    }

    if (!playlist_.empty()) {
        playIndex(0);
    }
}

PlaybackEngine::~PlaybackEngine() {
    stopDecoderThread();
}

void PlaybackEngine::update() {
    if (pendingAdvance_.exchange(false)) {
        const bool hasNextTrack = currentIndex_ + 1 < playlist_.size();
        if (hasNextTrack) {
            playIndexInternal(currentIndex_ + 1);
        } else {
            paused_.store(true);
            trackEnded_.store(false);
        }
    }

    std::filesystem::path activePath;
    {
        std::lock_guard lock(mutex_);
        if (!currentTrack_) {
            return;
        }
        activePath = currentTrack_->metadata.path;
    }

    auto waveform = waveformCache_.get(activePath);
    if (!waveform) {
        return;
    }

    std::lock_guard lock(mutex_);
    if (currentTrack_ && currentTrack_->metadata.path == activePath && currentTrack_->metadata.waveform != *waveform) {
        currentTrack_->metadata.waveform = std::move(*waveform);
    }
}

bool PlaybackEngine::playIndex(std::size_t index) {
    return playIndexInternal(index);
}

bool PlaybackEngine::playIndexInternal(std::size_t index) {
    if (index >= playlist_.size()) {
        return false;
    }

    loading_.store(true);
    lastError_.clear();
    stopDecoderThread();

    try {
        const auto trackPath = playlist_.pathAt(index);
        auto decoded = decoder_.decode(trackPath);
        if (auto cachedWaveform = waveformCache_.get(trackPath)) {
            decoded.metadata.waveform = std::move(*cachedWaveform);
        } else {
            waveformCache_.request(trackPath);
        }

        auto streamDecoder = std::make_unique<AudioStreamDecoder>();
        streamDecoder->open(trackPath);

        ringBuffer_.reset(kStreamBufferFrames, streamDecoder->channels());

        // Prime the buffer so playback starts immediately after track switch.
        std::vector<std::int16_t> prefill(kDecoderChunkFrames * static_cast<std::size_t>(streamDecoder->channels()));
        while (ringBuffer_.availableFrames() < AudioOutput::framesPerBuffer() * 2 && !streamDecoder->eof()) {
            const auto framesRead = streamDecoder->readFrames(prefill.data(), kDecoderChunkFrames);
            if (framesRead == 0) {
                ringBuffer_.markEof();
                break;
            }

            std::size_t pushedFrames = 0;
            while (pushedFrames < framesRead) {
                const auto written = ringBuffer_.push(
                    prefill.data() + static_cast<std::ptrdiff_t>(
                        pushedFrames * static_cast<std::size_t>(streamDecoder->channels())),
                    framesRead - pushedFrames);
                if (written == 0) {
                    break;
                }
                pushedFrames += written;
            }

            if (pushedFrames < framesRead) {
                break;
            }
        }

        {
            std::lock_guard lock(mutex_);
            currentTrack_ = std::move(decoded);
            currentIndex_ = index;
            playbackFrame_.store(0);
            paused_.store(false);
            trackEnded_.store(false);
            level_.store(0.0F);
            streamDecoder_ = std::move(streamDecoder);
        }

        stopDecoder_.store(false);
        decoderThread_ = std::thread([this]() { decoderLoop(); });
        loading_.store(false);
        return true;
    } catch (const std::exception& error) {
        std::lock_guard lock(mutex_);
        lastError_ = error.what();
        loading_.store(false);
        ringBuffer_.clear();
        return false;
    }
}

void PlaybackEngine::togglePause() {
    paused_.store(!paused_.load());
}

void PlaybackEngine::next() {
    if (playlist_.empty()) {
        return;
    }

    const auto nextIndex = std::min(currentIndex_ + 1, playlist_.size() - 1);
    playIndexInternal(nextIndex);
}

void PlaybackEngine::previous() {
    if (playlist_.empty()) {
        return;
    }

    const auto previousIndex = currentIndex_ == 0 ? 0 : currentIndex_ - 1;
    playIndexInternal(previousIndex);
}

void PlaybackEngine::adjustVolume(float delta) {
    volume_.store(clamp(volume_.load() + delta, 0.0F, 1.2F));
}

void PlaybackEngine::setVolume(float value) {
    volume_.store(clamp(value, 0.0F, 1.2F));
}

PlaybackSnapshot PlaybackEngine::snapshot() const {
    std::lock_guard lock(mutex_);

    PlaybackSnapshot result;
    result.paused = paused_.load();
    result.loading = loading_.load();
    result.currentIndex = currentIndex_;
    result.volume = volume_.load();
    result.level = level_.load();
    result.lastError = audioError_.empty() ? lastError_ : audioError_;

    if (!currentTrack_) {
        return result;
    }

    const auto totalFrames = static_cast<std::size_t>(
        currentTrack_->metadata.durationSeconds * AudioOutput::sampleRate());
    const auto renderedFrame = std::min(playbackFrame_.load(), totalFrames);
    const auto latencyFrames = output_ ? std::min(renderedFrame, AudioOutput::queuedLatencyFrames()) : 0UL;
    const auto currentFrame = renderedFrame - latencyFrames;

    result.hasTrack = true;
    result.title = currentTrack_->metadata.title;
    result.artist = currentTrack_->metadata.artist;
    result.album = currentTrack_->metadata.album;
    result.path = currentTrack_->metadata.path.string();
    result.durationSeconds = currentTrack_->metadata.durationSeconds;
    result.positionSeconds = static_cast<double>(currentFrame) / AudioOutput::sampleRate();
    result.waveform = currentTrack_->metadata.waveform;
    const auto activeBuffer = activeVisualizerBuffer_.load(std::memory_order_acquire);
    result.visualizer.assign(
        visualizerBuffers_[static_cast<std::size_t>(activeBuffer)].begin(),
        visualizerBuffers_[static_cast<std::size_t>(activeBuffer)].end());
    result.albumArt = currentTrack_->metadata.albumArt;
    result.lyrics = currentTrack_->metadata.lyrics;
    return result;
}

const Playlist& PlaybackEngine::playlist() const noexcept {
    return playlist_;
}

void PlaybackEngine::publishVisualizer(const std::array<float, kPlaybackVisualizerBins>& bins) noexcept {
    const int backBuffer = 1 - activeVisualizerBuffer_.load(std::memory_order_relaxed);
    visualizerBuffers_[static_cast<std::size_t>(backBuffer)] = bins;
    activeVisualizerBuffer_.store(backBuffer, std::memory_order_release);
}

void PlaybackEngine::stopDecoderThread() {
    stopDecoder_.store(true);
    if (decoderThread_.joinable()) {
        decoderThread_.join();
    }

    std::lock_guard lock(mutex_);
    if (streamDecoder_) {
        streamDecoder_->close();
        streamDecoder_.reset();
    }
    ringBuffer_.clear();
    ringBuffer_.clearEof();
}

void PlaybackEngine::decoderLoop() {
    std::vector<std::int16_t> decodeBuffer(kDecoderChunkFrames * 2, 0);

    while (!stopDecoder_.load()) {
        AudioStreamDecoder* streamDecoder = nullptr;
        {
            std::lock_guard lock(mutex_);
            streamDecoder = streamDecoder_.get();
        }

        if (streamDecoder == nullptr) {
            return;
        }

        if (ringBuffer_.freeFrames() < kDecoderChunkFrames / 2) {
            std::this_thread::sleep_for(kDecoderSleep);
            continue;
        }

        try {
            const auto framesRead = streamDecoder->readFrames(decodeBuffer.data(), kDecoderChunkFrames);
            if (framesRead == 0) {
                ringBuffer_.markEof();
                return;
            }

            std::size_t pushedFrames = 0;
            while (pushedFrames < framesRead && !stopDecoder_.load()) {
                const auto written = ringBuffer_.push(
                    decodeBuffer.data() + static_cast<std::ptrdiff_t>(pushedFrames * 2),
                    framesRead - pushedFrames);
                if (written == 0) {
                    std::this_thread::sleep_for(kDecoderSleep);
                    continue;
                }
                pushedFrames += written;
            }
        } catch (const std::exception& error) {
            std::lock_guard lock(mutex_);
            lastError_ = error.what();
            ringBuffer_.markEof();
            return;
        }
    }
}

std::size_t PlaybackEngine::renderFrames(std::int16_t* destination, std::size_t frames) {
    if (destination == nullptr || frames == 0) {
        return 0;
    }

    if (paused_.load() || loading_.load()) {
        std::fill(destination, destination + frames * 2, 0);
        level_.store(0.0F);
        publishVisualizer({});
        return frames;
    }

    {
        std::lock_guard lock(mutex_);
        if (!currentTrack_) {
            std::fill(destination, destination + frames * 2, 0);
            level_.store(0.0F);
            publishVisualizer({});
            return frames;
        }
    }

    const auto framesRead = ringBuffer_.pop(destination, frames);
    const int channels = std::max(1, ringBuffer_.channels());
    const float gain = volume_.load();

    if (framesRead < frames) {
        std::fill(
            destination + framesRead * static_cast<std::size_t>(channels),
            destination + frames * static_cast<std::size_t>(channels),
            0);
    }

    float peak = 0.0F;
    for (std::size_t frame = 0; frame < framesRead; ++frame) {
        for (int channel = 0; channel < channels; ++channel) {
            const auto sampleIndex = frame * static_cast<std::size_t>(channels) + static_cast<std::size_t>(channel);
            destination[sampleIndex] = applyGain(destination[sampleIndex], gain);
            const float sample = static_cast<float>(destination[sampleIndex]) / kInt16Scale;
            peak = std::max(peak, std::abs(sample));
        }
    }

    playbackFrame_.store(playbackFrame_.load() + framesRead);
    level_.store(peak);
    publishVisualizer(buildVisualizerWindow(destination, framesRead, channels));

    if (framesRead < frames && ringBuffer_.eof()) {
        if (!trackEnded_.exchange(true)) {
            pendingAdvance_.store(true);
        }
    }

    return frames;
}

}  // namespace retrowave
