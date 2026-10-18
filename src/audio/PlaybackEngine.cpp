// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#include "audio/PlaybackEngine.h"

#include "audio/AudioOutput.h"
#include "audio/AudioStreamDecoder.h"
#include "core/EventHooks.h"


#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <numeric>
#include <random>
#include <thread>
#include <vector>

namespace retrowave {
namespace {

constexpr float kInt16Scale = 32767.0F;
constexpr std::size_t kStreamBufferFrames = 32768;
constexpr std::size_t kDecoderChunkFrames = 2048;
constexpr auto kDecoderSleep = std::chrono::milliseconds(12);

std::int16_t applyGain(std::int16_t sample, float gain) {
    const float scaled = static_cast<float>(sample) * gain;
    const float clamped = std::clamp(scaled, -kInt16Scale, kInt16Scale);
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

DecodedTrack makeRemoteFallbackTrack(const std::string& source, const std::string& titleHint) {
    DecodedTrack track;
    track.metadata.path = std::filesystem::path(source);
    track.metadata.source = source;
    track.metadata.remote = true;
    track.metadata.live = true;
    track.metadata.title = titleHint.empty() ? source : titleHint;
    track.metadata.durationSeconds = 0.0;
    auto lyrics = std::make_shared<LyricsData>();
    lyrics->message = "Lyrics are unavailable for remote streams.";
    track.metadata.lyrics = lyrics;
    return track;
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

    loaderThread_ = std::thread([this]() { loaderLoop(); });
    if (!playlist_.empty()) {
        playIndex(0);
    }
}

PlaybackEngine::~PlaybackEngine() {
    finalizeWaveformSampling();
    stopLoader_.store(true);
    loaderCv_.notify_one();
    if (loaderThread_.joinable()) {
        loaderThread_.join();
    }
    stopDecoderThread();
}

void PlaybackEngine::update() {
    {
        std::lock_guard lock(mutex_);
        if (currentTrack_ && state() != PlaybackState::Loading && ringBuffer_.eof() &&
            ringBuffer_.availableFrames() == 0 && !trackEnded_.exchange(true)) {
            pendingAdvance_.store(true);
        }
    }

    if (pendingAdvance_.exchange(false)) {
        const auto currentIndex = currentIndex_.load();
        if (const auto nextIndex = queue_.getNext(currentIndex, playlist_.size())) {
            playIndexInternal(*nextIndex);
        } else {
            setState(PlaybackState::Ended);
            trackEnded_.store(true);
        }
    }

    std::filesystem::path activePath;
    bool activeRemote = false;
    {
        std::lock_guard lock(mutex_);
        if (!currentTrack_) {
            return;
        }
        activePath = currentTrack_->metadata.path;
        activeRemote = currentTrack_->metadata.remote;
    }

    if (activeRemote) {
        return;
    }

    if (samplingActive_.load(std::memory_order_relaxed)) {
        std::lock_guard lock(mutex_);
        if (currentTrack_ && currentTrack_->metadata.path == samplingPath_) {
            currentTrack_->metadata.waveform.resize(samplingBins_.size());
            for (std::size_t i = 0; i < samplingBins_.size(); ++i) {
                currentTrack_->metadata.waveform[i] = samplingBins_[i].load(std::memory_order_relaxed);
            }
        }
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
    queue_.clearHistory();
    return playIndexInternal(index);
}

bool PlaybackEngine::playIndexInternal(std::size_t index) {
    if (index >= playlist_.size()) {
        return false;
    }

    queue_.onTrackSelected(index, playlist_.size());

    const auto generation = loadGeneration_.fetch_add(1, std::memory_order_acq_rel) + 1;
    loadingIndex_.store(index);
    setState(PlaybackState::Loading);
    pendingAdvance_.store(false);
    trackEnded_.store(false);
    {
        std::lock_guard lock(mutex_);
        lastError_.clear();
    }
    {
        std::lock_guard lock(loaderMutex_);
        requestedLoadIndex_ = index;
        requestedLoadGeneration_ = generation;
        loadRequested_ = true;
    }
    loaderCv_.notify_one();
    return true;
}

void PlaybackEngine::loaderLoop() {
    while (!stopLoader_.load()) {
        std::size_t index = 0;
        std::uint64_t generation = 0;
        {
            std::unique_lock lock(loaderMutex_);
            loaderCv_.wait(lock, [this]() { return stopLoader_.load() || loadRequested_; });
            if (stopLoader_.load()) {
                return;
            }
            index = requestedLoadIndex_;
            generation = requestedLoadGeneration_;
            loadRequested_ = false;
        }

        loadIndex(index, generation);
    }
}

void PlaybackEngine::finalizeWaveformSampling() {
    if (!samplingActive_.load(std::memory_order_relaxed)) {
        return;
    }

    std::vector<float> waveform(samplingBins_.size());
    for (std::size_t i = 0; i < samplingBins_.size(); ++i) {
        waveform[i] = samplingBins_[i].load(std::memory_order_relaxed);
    }
    const auto path = samplingPath_;
    const bool sparse =
        std::count_if(waveform.begin(), waveform.end(), [](float value) { return value > 0.001F; }) < 8;

    samplingActive_.store(false, std::memory_order_release);
    samplingPath_.clear();
    samplingTotalFrames_.store(0, std::memory_order_relaxed);
    for (auto& bin : samplingBins_) {
        bin.store(0.0F, std::memory_order_relaxed);
    }

    if (path.empty()) {
        return;
    }

    if (!sparse) {
        waveformCache_.store(path, std::move(waveform));
    }
}

void PlaybackEngine::ensureLyrics() {
    std::lock_guard lock(mutex_);
    if (!currentTrack_ || currentTrack_->metadata.remote || currentTrack_->metadata.lyricsResolved) {
        return;
    }

    const LyricsLoader loader;
    const auto embedded = std::move(currentTrack_->metadata.embeddedLyricsScratch);
    currentTrack_->metadata.lyrics = std::make_shared<LyricsData>(
        loader.loadForTrack(currentTrack_->metadata.path, embedded));
    currentTrack_->metadata.lyricsResolved = true;
}

void PlaybackEngine::sampleWaveformFrame(std::size_t absoluteFrame, float peak) noexcept {
    const auto totalFrames = samplingTotalFrames_.load(std::memory_order_relaxed);
    if (!samplingActive_.load(std::memory_order_relaxed) || totalFrames == 0 || peak <= 0.0F) {
        return;
    }

    const std::size_t bin = std::min(
        kWaveformBinCount - 1,
        (absoluteFrame * kWaveformBinCount) / totalFrames);
    
    float current = samplingBins_[bin].load(std::memory_order_relaxed);
    while (peak > current) {
        if (samplingBins_[bin].compare_exchange_weak(current, peak, std::memory_order_relaxed)) {
            break;
        }
    }
}

bool PlaybackEngine::loadStillCurrent(std::uint64_t generation) const noexcept {
    return generation == loadGeneration_.load(std::memory_order_acquire) && !stopLoader_.load();
}

void PlaybackEngine::loadIndex(std::size_t index, std::uint64_t generation) {
    try {
        finalizeWaveformSampling();
        if (!loadStillCurrent(generation)) {
            return;
        }

        const bool remote = playlist_.isRemoteAt(index);
        const auto source = playlist_.sourceAt(index);
        const auto titleHint = playlist_.titleAt(index);
        const auto trackPath = remote ? std::filesystem::path(source) : playlist_.pathAt(index);
        auto streamDecoder = std::make_unique<AudioStreamDecoder>();
        DecodedTrack decoded;
        if (remote) {
            decoded = makeRemoteFallbackTrack(source, titleHint);
            decoded.metadata.waveform.clear();
            streamDecoder->openUrl(source);
            if (!loadStillCurrent(generation)) {
                return;
            }
            if (decoded.metadata.durationSeconds <= 0.0 && streamDecoder->durationSeconds() > 0.0) {
                decoded.metadata.durationSeconds = streamDecoder->durationSeconds();
            }
            decoded.metadata.live = decoded.metadata.durationSeconds <= 0.0 || streamDecoder->live();
        } else {
            streamDecoder->open(trackPath);
            if (!loadStillCurrent(generation)) {
                return;
            }

            if (const auto cachedMetadata = metadataCache_.get(trackPath)) {
                decoded.metadata = *cachedMetadata;
            } else {
                decoded.metadata = decoder_.probeLocalMetadata(
                    streamDecoder->formatContext(),
                    streamDecoder->audioStreamIndex(),
                    trackPath);
                TrackMetadata metadataForCache = decoded.metadata;
                metadataForCache.waveform.clear();
                metadataForCache.lyricsResolved = false;
                metadataCache_.store(trackPath, std::move(metadataForCache));
            }

            if (!loadStillCurrent(generation)) {
                return;
            }

            if (const auto cachedWaveform = waveformCache_.get(trackPath)) {
                decoded.metadata.waveform = *cachedWaveform;
            } else {
                samplingPath_ = trackPath;
                for (auto& bin : samplingBins_) {
                    bin.store(0.0F, std::memory_order_relaxed);
                }
                const auto totalFrames = decoded.metadata.durationSeconds > 0.0
                    ? std::max<std::size_t>(
                          1,
                          static_cast<std::size_t>(
                              decoded.metadata.durationSeconds * static_cast<double>(AudioOutput::sampleRate())))
                    : 0;
                samplingTotalFrames_.store(totalFrames, std::memory_order_relaxed);
                samplingActive_.store(totalFrames > 0, std::memory_order_release);
            }
        }

        if (!loadStillCurrent(generation)) {
            return;
        }

        stopDecoderThread();
        ringBuffer_.reset(kStreamBufferFrames, streamDecoder->channels());

        if (!remote) {
            // Prime local files so playback starts immediately after track switch.
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
        }

        if (!loadStillCurrent(generation)) {
            ringBuffer_.flush();
            
            return;
        }

        std::string titleStr, artistStr, albumStr, pathStr;
        int durationSec = 0;
        {
            std::lock_guard lock(mutex_);
            currentTrack_ = std::move(decoded);
            currentTrack_->metadata.lyricsResolved = false;
            
            titleStr = currentTrack_->metadata.title;
            artistStr = currentTrack_->metadata.artist;
            albumStr = currentTrack_->metadata.album;
            durationSec = static_cast<int>(currentTrack_->metadata.durationSeconds);
            pathStr = currentTrack_->metadata.path;

            currentIndex_.store(index);
            playbackFrame_.store(0);
            trackEnded_.store(false);
            level_.store(0.0F);
            currentRemote_.store(remote);
            currentLive_.store(currentTrack_->metadata.live);
            streamDecoder_ = std::move(streamDecoder);

            stopDecoder_.store(false, std::memory_order_release);
            hasTrack_.store(true, std::memory_order_release);
            decoderThread_ = std::thread([this]() { decoderLoop(); });
            setState(PlaybackState::Playing);
        }
        EventHooks::onTrackChange(titleStr, artistStr, albumStr, durationSec, pathStr);
    } catch (const std::exception& error) {
        samplingActive_.store(false, std::memory_order_release);
        if (!loadStillCurrent(generation)) {
            return;
        }
        std::lock_guard lock(mutex_);
        lastError_ = error.what();
        setState(PlaybackState::Failed);
        ringBuffer_.flush();
    }
}

void PlaybackEngine::togglePause() {
    const auto currentState = state();
    if (currentState == PlaybackState::Playing || currentState == PlaybackState::Buffering) {
        setState(PlaybackState::Paused);
    } else if (currentState == PlaybackState::Paused) {
        setState(PlaybackState::Playing);
    }
}

void PlaybackEngine::next() {
    if (playlist_.empty()) {
        return;
    }

    const auto currentIndex = navigationBaseIndex();
    if (const auto nextIndex = queue_.getNext(currentIndex, playlist_.size())) {
        playIndexInternal(*nextIndex);
    }
}

void PlaybackEngine::previous() {
    if (playlist_.empty()) {
        return;
    }

    const auto currentIndex = navigationBaseIndex();
    playIndexInternal(queue_.getPrevious(currentIndex, playlist_.size()));
}

void PlaybackEngine::adjustVolume(float delta) {
    volume_.store(std::clamp(volume_.load() + delta, 0.0F, 1.2F));
}

bool PlaybackEngine::seek(double seconds) {
    std::lock_guard lock(mutex_);
    if (state() == PlaybackState::Loading || !currentTrack_ || currentTrack_->metadata.remote || currentTrack_->metadata.live || !streamDecoder_) {
        return false;
    }

    const double clamped = std::clamp(seconds, 0.0, currentTrack_->metadata.durationSeconds);
    requestedSeekSeconds_.store(clamped, std::memory_order_release);
    setState(PlaybackState::Playing);
    
    return true;
}

void PlaybackEngine::setVolume(float value) {
    volume_.store(std::clamp(value, 0.0F, 1.2F));
}

void PlaybackEngine::setRepeatMode(RepeatMode mode) {
    queue_.setRepeatMode(mode);
}

RepeatMode PlaybackEngine::repeatMode() const noexcept {
    return queue_.repeatMode();
}

void PlaybackEngine::setShuffleMode(ShuffleMode mode) {
    queue_.setShuffleMode(mode, navigationBaseIndex(), playlist_.size());
}

ShuffleMode PlaybackEngine::shuffleMode() const noexcept {
    return queue_.shuffleMode();
}

PlaybackSnapshot PlaybackEngine::snapshot() const {
    std::lock_guard lock(mutex_);

    PlaybackSnapshot result;
    result.state = state();
    result.paused = result.state == PlaybackState::Paused || result.state == PlaybackState::Ended ||
        result.state == PlaybackState::Failed || result.state == PlaybackState::Idle;
    result.loading = result.state == PlaybackState::Loading;
    result.currentIndex = currentIndex_.load();
    result.volume = volume_.load();
    result.level = level_.load();
    result.repeatMode = repeatMode();
    result.shuffleMode = shuffleMode();
    result.lastError = audioError_.empty() ? lastError_ : audioError_;

    if (!currentTrack_) {
        return result;
    }

    const auto renderedFrame = playbackFrame_.load();
    const auto durationFrames = currentTrack_->metadata.durationSeconds > 0.0
        ? static_cast<std::size_t>(currentTrack_->metadata.durationSeconds * AudioOutput::sampleRate())
        : renderedFrame;
    const auto boundedRenderedFrame = std::min(renderedFrame, durationFrames);
    const auto latencyFrames = output_ ? std::min(boundedRenderedFrame, AudioOutput::queuedLatencyFrames()) : 0UL;
    const auto currentFrame = boundedRenderedFrame - latencyFrames;

    result.hasTrack = true;
    result.remote = currentTrack_->metadata.remote;
    result.live = currentTrack_->metadata.live;
    result.title = currentTrack_->metadata.title;
    result.artist = currentTrack_->metadata.artist;
    result.album = currentTrack_->metadata.album;
    result.path = currentTrack_->metadata.source.empty()
        ? currentTrack_->metadata.path.string()
        : currentTrack_->metadata.source;
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
    std::thread threadToJoin;
    std::unique_ptr<AudioStreamDecoder> decoderToClose;

    hasTrack_.store(false, std::memory_order_release);
    stopDecoder_.store(true, std::memory_order_release);

    {
        std::lock_guard lock(mutex_);
        if (streamDecoder_) {
            streamDecoder_->requestStop();
        }
        threadToJoin = std::move(decoderThread_);
        decoderToClose = std::move(streamDecoder_);
    }

    if (threadToJoin.joinable()) {
        threadToJoin.join();
    }

    if (decoderToClose) {
        decoderToClose->close();
    }
    ringBuffer_.flush();
    
    currentRemote_.store(false);
    currentLive_.store(false);
}

std::size_t PlaybackEngine::navigationBaseIndex() const {
    return state() == PlaybackState::Loading ? loadingIndex_.load() : currentIndex_.load();
}

PlaybackState PlaybackEngine::state() const noexcept {
    return state_.load();
}

void PlaybackEngine::setState(PlaybackState state) noexcept {
    PlaybackState oldState = state_.exchange(state);
    if (oldState != state) {
        std::string stateStr;
        switch (state) {
            case PlaybackState::Playing: stateStr = "playing"; break;
            case PlaybackState::Paused: stateStr = "paused"; break;
            case PlaybackState::Idle:
            case PlaybackState::Ended:
            case PlaybackState::Failed: stateStr = "stopped"; break;
            default: break;
        }
        if (!stateStr.empty()) {
            EventHooks::onStateChange(stateStr);
        }
    }
}

void PlaybackEngine::stop() {
    stopDecoderThread();
    setState(PlaybackState::Idle);
}

bool PlaybackEngine::playbackBlocked() const noexcept {
    const auto currentState = state();
    return currentState == PlaybackState::Idle ||
        currentState == PlaybackState::Loading ||
        currentState == PlaybackState::Paused ||
        currentState == PlaybackState::Ended ||
        currentState == PlaybackState::Failed;
}



void PlaybackEngine::decoderLoop() {
    decoderThreadActive_.store(true, std::memory_order_release);
    std::vector<std::int16_t> decodeBuffer(kDecoderChunkFrames * 2, 0);

    while (!stopDecoder_.load()) {
        AudioStreamDecoder* streamDecoder = nullptr;
        {
            std::lock_guard lock(mutex_);
            streamDecoder = streamDecoder_.get();
        }

        if (streamDecoder == nullptr) {
            decoderThreadActive_.store(false, std::memory_order_release);
            return;
        }

        const double seekTarget = requestedSeekSeconds_.exchange(-1.0, std::memory_order_acquire);
        if (seekTarget >= 0.0) {
            if (streamDecoder->seekSeconds(seekTarget)) {
                ringBuffer_.flush();
                playbackFrame_.store(static_cast<std::size_t>(seekTarget * static_cast<double>(AudioOutput::sampleRate())), std::memory_order_relaxed);
                trackEnded_.store(false, std::memory_order_relaxed);
                pendingAdvance_.store(false, std::memory_order_relaxed);
            }
        }

        if (ringBuffer_.freeFrames() < kDecoderChunkFrames / 2) {
            std::this_thread::sleep_for(kDecoderSleep);
            continue;
        }

        try {
            const auto framesRead = streamDecoder->readFrames(decodeBuffer.data(), kDecoderChunkFrames);
            if (framesRead == 0) {
                if (currentRemote_.load()) {
                    std::lock_guard lock(mutex_);
                    lastError_ = currentLive_.load()
                        ? "Live stream ended or connection was interrupted."
                        : "Remote track ended or produced no audio.";
                }
                ringBuffer_.markEof();
                decoderThreadActive_.store(false, std::memory_order_release);
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
            decoderThreadActive_.store(false, std::memory_order_release);
            return;
        }
    }
    decoderThreadActive_.store(false, std::memory_order_release);
}

std::size_t PlaybackEngine::renderFrames(std::int16_t* destination, std::size_t frames) {
    if (destination == nullptr || frames == 0) {
        return 0;
    }

    if (playbackBlocked() || !hasTrack_.load(std::memory_order_acquire)) {
        std::fill_n(destination, frames * 2, 0);
        level_.store(0.0F, std::memory_order_relaxed);
        publishVisualizer({});
        return frames;
    }

    const auto framesRead = ringBuffer_.pop(destination, frames);
    const int channels = std::max(1, ringBuffer_.channels());
    const float gain = volume_.load(std::memory_order_relaxed);

    if (framesRead < frames) {
        std::fill_n(destination + framesRead * static_cast<std::size_t>(channels),
                    (frames - framesRead) * static_cast<std::size_t>(channels), 0);
    }

    const std::size_t frameBase = playbackFrame_.load(std::memory_order_relaxed);
    float peak = 0.0F;
    for (std::size_t frame = 0; frame < framesRead; ++frame) {
        float framePeak = 0.0F;
        for (int channel = 0; channel < channels; ++channel) {
            const auto sampleIndex = frame * static_cast<std::size_t>(channels) + static_cast<std::size_t>(channel);
            destination[sampleIndex] = applyGain(destination[sampleIndex], gain);
            const float sample = static_cast<float>(destination[sampleIndex]) / kInt16Scale;
            framePeak = std::max(framePeak, std::abs(sample));
        }
        peak = std::max(peak, framePeak);
        sampleWaveformFrame(frameBase + frame, framePeak);
    }

    playbackFrame_.store(frameBase + framesRead, std::memory_order_relaxed);
    level_.store(peak, std::memory_order_relaxed);
    publishVisualizer(buildVisualizerWindow(destination, framesRead, channels));

    if (framesRead < frames && ringBuffer_.eof()) {
        if (!trackEnded_.exchange(true, std::memory_order_acq_rel)) {
            pendingAdvance_.store(true, std::memory_order_release);
        }
    }

    return frames;
}

}  // namespace retrowave
