#include "audio/PlaybackEngine.h"

#include "audio/AudioOutput.h"

#include <algorithm>
#include <cmath>

namespace retrowave {
namespace {

constexpr float kInt16Scale = 32767.0F;
constexpr std::size_t kVisualizerBins = 64;

float clamp(float value, float minimum, float maximum) {
    return std::max(minimum, std::min(maximum, value));
}

std::vector<float> buildVisualizerWindow(const std::int16_t* samples, std::size_t frameCount, int channels) {
    std::vector<float> bins(kVisualizerBins, 0.0F);
    if (samples == nullptr || frameCount == 0 || channels <= 0) {
        return bins;
    }

    const auto framesPerBin = std::max<std::size_t>(1, frameCount / kVisualizerBins);

    for (std::size_t bin = 0; bin < kVisualizerBins; ++bin) {
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

PlaybackEngine::PlaybackEngine(Playlist playlist) : playlist_(std::move(playlist)) {
    visualizerBins_.assign(kVisualizerBins, 0.0F);
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

PlaybackEngine::~PlaybackEngine() = default;

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

    try {
        auto decoded = decoder_.decode(playlist_.at(index).path);

        {
            std::lock_guard lock(mutex_);
            currentTrack_ = std::move(decoded);
            currentIndex_ = index;
            playbackFrame_.store(0);
            paused_.store(false);
            trackEnded_.store(false);
            level_.store(0.0F);
            visualizerBins_.assign(kVisualizerBins, 0.0F);
        }

        loading_.store(false);
        return true;
    } catch (const std::exception& error) {
        std::lock_guard lock(mutex_);
        lastError_ = error.what();
        loading_.store(false);
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

    const auto totalFrames =
        currentTrack_->samples.size() / static_cast<std::size_t>(std::max(1, currentTrack_->channels));
    const auto renderedFrame = std::min(playbackFrame_.load(), totalFrames);
    const auto latencyFrames = output_ ? std::min(renderedFrame, AudioOutput::queuedLatencyFrames()) : 0UL;
    const auto currentFrame = renderedFrame - latencyFrames;

    result.hasTrack = true;
    result.title = currentTrack_->title;
    result.artist = currentTrack_->artist;
    result.album = currentTrack_->album;
    result.path = currentTrack_->path.string();
    result.durationSeconds = currentTrack_->durationSeconds;
    result.positionSeconds = static_cast<double>(currentFrame) / static_cast<double>(currentTrack_->sampleRate);
    result.waveform = currentTrack_->waveform;
    result.visualizer = visualizerBins_;
    result.albumArt = currentTrack_->albumArt;
    result.lyrics = currentTrack_->lyrics;
    return result;
}

const Playlist& PlaybackEngine::playlist() const noexcept {
    return playlist_;
}

std::size_t PlaybackEngine::renderFrames(std::int16_t* destination, std::size_t frames) {
    std::lock_guard lock(mutex_);

    if (!currentTrack_ || paused_.load() || loading_.load()) {
        std::fill(destination, destination + frames * 2, 0);
        level_.store(0.0F);
        std::fill(visualizerBins_.begin(), visualizerBins_.end(), 0.0F);
        return frames;
    }

    const auto totalFrames =
        currentTrack_->samples.size() / static_cast<std::size_t>(std::max(1, currentTrack_->channels));
    auto frameIndex = playbackFrame_.load();

    if (frameIndex >= totalFrames) {
        std::fill(destination, destination + frames * 2, 0);
        level_.store(0.0F);
        std::fill(visualizerBins_.begin(), visualizerBins_.end(), 0.0F);

        if (!trackEnded_.exchange(true)) {
            pendingAdvance_.store(true);
        }

        return frames;
    }

    const auto remainingFrames = totalFrames - frameIndex;
    const auto framesToWrite = std::min(frames, remainingFrames);
    const float volume = volume_.load();

    float peak = 0.0F;
    for (std::size_t frame = 0; frame < framesToWrite; ++frame) {
        for (int channel = 0; channel < currentTrack_->channels; ++channel) {
            const auto sampleIndex =
                (frameIndex + frame) * static_cast<std::size_t>(currentTrack_->channels) + static_cast<std::size_t>(channel);
            const float sample =
                clamp(static_cast<float>(currentTrack_->samples[sampleIndex]) / kInt16Scale * volume, -1.0F, 1.0F);
            peak = std::max(peak, std::abs(sample));
            destination[frame * static_cast<std::size_t>(currentTrack_->channels) + static_cast<std::size_t>(channel)] =
                static_cast<std::int16_t>(std::lrint(sample * kInt16Scale));
        }
    }

    if (framesToWrite < frames) {
        std::fill(
            destination + framesToWrite * static_cast<std::size_t>(currentTrack_->channels),
            destination + frames * static_cast<std::size_t>(currentTrack_->channels),
            0);
        if (!trackEnded_.exchange(true)) {
            pendingAdvance_.store(true);
        }
    }

    playbackFrame_.store(frameIndex + framesToWrite);
    level_.store(peak);
    visualizerBins_ = buildVisualizerWindow(destination, framesToWrite, currentTrack_->channels);
    return frames;
}

}  // namespace retrowave
