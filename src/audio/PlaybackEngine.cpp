#include "audio/PlaybackEngine.h"

#include "audio/AudioOutput.h"
#include "audio/AudioStreamDecoder.h"

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
constexpr std::size_t kStreamBufferFrames = static_cast<std::size_t>(AudioOutput::sampleRate()) / 2;
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
        if (const auto nextIndex = computeNextIndex(currentIndex)) {
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

    if (samplingActive_) {
        std::lock_guard lock(mutex_);
        if (currentTrack_ && currentTrack_->metadata.path == samplingPath_) {
            currentTrack_->metadata.waveform.assign(samplingBins_.begin(), samplingBins_.end());
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
    clearShuffleHistory();
    return playIndexInternal(index);
}

bool PlaybackEngine::playIndexInternal(std::size_t index) {
    if (index >= playlist_.size()) {
        return false;
    }

    if (shuffleMode() == ShuffleMode::On) {
        syncShufflePosition(index);
    }

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
    if (!samplingActive_) {
        return;
    }

    std::vector<float> waveform(samplingBins_.begin(), samplingBins_.end());
    const auto path = samplingPath_;
    const bool sparse =
        std::count_if(waveform.begin(), waveform.end(), [](float value) { return value > 0.001F; }) < 8;

    samplingActive_ = false;
    samplingPath_.clear();
    samplingTotalFrames_ = 0;
    samplingBins_.fill(0.0F);

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
    if (!samplingActive_ || samplingTotalFrames_ == 0 || peak <= 0.0F) {
        return;
    }

    const std::size_t bin = std::min(
        kWaveformBinCount - 1,
        (absoluteFrame * kWaveformBinCount) / samplingTotalFrames_);
    samplingBins_[bin] = std::max(samplingBins_[bin], peak);
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
                samplingBins_.fill(0.0F);
                samplingTotalFrames_ = decoded.metadata.durationSeconds > 0.0
                    ? std::max<std::size_t>(
                          1,
                          static_cast<std::size_t>(
                              decoded.metadata.durationSeconds * static_cast<double>(AudioOutput::sampleRate())))
                    : 0;
                samplingActive_ = samplingTotalFrames_ > 0;
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
            ringBuffer_.clear();
            ringBuffer_.clearEof();
            return;
        }

        {
            std::lock_guard lock(mutex_);
            currentTrack_ = std::move(decoded);
            currentTrack_->metadata.lyricsResolved = false;
            currentIndex_.store(index);
            playbackFrame_.store(0);
            trackEnded_.store(false);
            level_.store(0.0F);
            currentRemote_.store(remote);
            currentLive_.store(decoded.metadata.live);
            streamDecoder_ = std::move(streamDecoder);
        }

        // Wait for any previous decoder thread to fully exit before starting a new one
        while (decoderThreadActive_.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        stopDecoder_.store(false);
        decoderThread_ = std::thread([this]() { decoderLoop(); });
        setState(PlaybackState::Playing);
    } catch (const std::exception& error) {
        samplingActive_ = false;
        if (!loadStillCurrent(generation)) {
            return;
        }
        std::lock_guard lock(mutex_);
        lastError_ = error.what();
        setState(PlaybackState::Failed);
        ringBuffer_.clear();
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
    if (const auto nextIndex = computeNextIndex(currentIndex)) {
        playIndexInternal(*nextIndex);
    }
}

void PlaybackEngine::previous() {
    if (playlist_.empty()) {
        return;
    }

    const auto currentIndex = navigationBaseIndex();
    playIndexInternal(computePreviousIndex(currentIndex));
}

void PlaybackEngine::adjustVolume(float delta) {
    volume_.store(std::clamp(volume_.load() + delta, 0.0F, 1.2F));
}

bool PlaybackEngine::seek(double seconds) {
    // Stop the decoder thread to prevent concurrent access to streamDecoder_
    stopDecoder_.store(true);
    {
        std::lock_guard lock(mutex_);
        if (streamDecoder_) {
            streamDecoder_->requestStop();
        }
    }

    if (decoderThread_.joinable()) {
        decoderThread_.join();
    }

    // Wait for decoderLoop to fully exit and mark itself inactive
    while (decoderThreadActive_.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    std::lock_guard lock(mutex_);
    if (!currentTrack_ || currentTrack_->metadata.remote || currentTrack_->metadata.live) {
        while (decoderThreadActive_.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        stopDecoder_.store(false);
        decoderThread_ = std::thread([this]() { decoderLoop(); });
        return false;
    }
    if (streamDecoder_ == nullptr) {
        while (decoderThreadActive_.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        stopDecoder_.store(false);
        decoderThread_ = std::thread([this]() { decoderLoop(); });
        return false;
    }

    const double clamped = std::clamp(seconds, 0.0, currentTrack_->metadata.durationSeconds);
    if (!streamDecoder_->seekSeconds(clamped)) {
        while (decoderThreadActive_.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        stopDecoder_.store(false);
        decoderThread_ = std::thread([this]() { decoderLoop(); });
        return false;
    }

    ringBuffer_.clear();
    ringBuffer_.clearEof();
    playbackFrame_.store(static_cast<std::size_t>(clamped * AudioOutput::sampleRate()));
    trackEnded_.store(false);
    pendingAdvance_.store(false);
    level_.store(0.0F);
    setState(PlaybackState::Playing);

    // Wait for decoderLoop to fully exit before restarting
    while (decoderThreadActive_.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    stopDecoder_.store(false);
    decoderThread_ = std::thread([this]() { decoderLoop(); });
    return true;
}

void PlaybackEngine::setVolume(float value) {
    volume_.store(std::clamp(value, 0.0F, 1.2F));
}

void PlaybackEngine::setRepeatMode(RepeatMode mode) {
    repeatMode_.store(mode);
}

RepeatMode PlaybackEngine::repeatMode() const noexcept {
    return repeatMode_.load();
}

void PlaybackEngine::setShuffleMode(ShuffleMode mode) {
    shuffleMode_.store(mode);
    if (mode == ShuffleMode::On) {
        syncShufflePosition(navigationBaseIndex());
        clearShuffleHistory();
    } else {
        std::lock_guard lock(shuffleMutex_);
        shuffleOrder_.clear();
        playbackHistory_.clear();
    }
}

ShuffleMode PlaybackEngine::shuffleMode() const noexcept {
    return shuffleMode_.load();
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
    stopDecoder_.store(true);

    {
        std::lock_guard lock(mutex_);
        if (streamDecoder_) {
            streamDecoder_->requestStop();
        }
    }

    if (decoderThread_.joinable()) {
        decoderThread_.join();
    }

    std::unique_ptr<AudioStreamDecoder> decoderToClose;
    {
        std::lock_guard lock(mutex_);
        decoderToClose = std::move(streamDecoder_);
    }
    if (decoderToClose) {
        decoderToClose->close();
    }
    ringBuffer_.clear();
    ringBuffer_.clearEof();
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
    state_.store(state);
}

bool PlaybackEngine::playbackBlocked() const noexcept {
    const auto currentState = state();
    return currentState == PlaybackState::Idle ||
        currentState == PlaybackState::Loading ||
        currentState == PlaybackState::Paused ||
        currentState == PlaybackState::Ended ||
        currentState == PlaybackState::Failed;
}

void PlaybackEngine::clearShuffleHistory() {
    std::lock_guard lock(shuffleMutex_);
    playbackHistory_.clear();
    playbackHistory_.shrink_to_fit();
}

void PlaybackEngine::trimShuffleHistory() {
    if (playbackHistory_.size() <= kMaxPlaybackHistory) {
        return;
    }

    const auto eraseCount = playbackHistory_.size() - kMaxPlaybackHistory;
    playbackHistory_.erase(playbackHistory_.begin(), playbackHistory_.begin() + static_cast<std::ptrdiff_t>(eraseCount));
}

void PlaybackEngine::rebuildShuffleOrder(std::size_t anchorIndex) {
    const auto count = playlist_.size();
    shuffleOrder_.resize(count);
    if (count == 0) {
        shufflePosition_ = 0;
        return;
    }

    std::iota(shuffleOrder_.begin(), shuffleOrder_.end(), std::size_t{0});
    if (count > 1) {
        std::random_device rd;
        std::mt19937 gen(rd());
        for (std::size_t i = count - 1; i > 0; --i) {
            std::uniform_int_distribution<std::size_t> dis(0, i);
            std::swap(shuffleOrder_[i], shuffleOrder_[dis(gen)]);
        }
    }

    shufflePosition_ = findShufflePosition(anchorIndex);
}

std::size_t PlaybackEngine::findShufflePosition(std::size_t index) const {
    for (std::size_t position = 0; position < shuffleOrder_.size(); ++position) {
        if (shuffleOrder_[position] == index) {
            return position;
        }
    }
    return 0;
}

void PlaybackEngine::syncShufflePosition(std::size_t index) {
    std::lock_guard lock(shuffleMutex_);
    if (shuffleOrder_.size() != playlist_.size()) {
        rebuildShuffleOrder(index);
        return;
    }
    shufflePosition_ = findShufflePosition(index);
}

std::size_t PlaybackEngine::startNewShufflePass(std::size_t avoidIndex) {
    rebuildShuffleOrder(avoidIndex);
    if (shuffleOrder_.size() <= 1) {
        shufflePosition_ = 0;
        return shuffleOrder_.empty() ? 0 : shuffleOrder_.front();
    }

    shufflePosition_ = 0;
    if (shuffleOrder_[0] != avoidIndex) {
        return shuffleOrder_[0];
    }

    shufflePosition_ = 1;
    return shuffleOrder_[1];
}

std::optional<std::size_t> PlaybackEngine::computeNextIndex(std::size_t currentIndex) {
    const auto count = playlist_.size();
    if (count == 0) {
        return std::nullopt;
    }

    if (shuffleMode() == ShuffleMode::Off) {
        if (currentIndex + 1 < count) {
            return currentIndex + 1;
        }

        switch (repeatMode()) {
            case RepeatMode::One:
                return currentIndex;
            case RepeatMode::All:
                return 0;
            case RepeatMode::Off:
                return std::nullopt;
        }
    }

    std::lock_guard lock(shuffleMutex_);
    if (shuffleOrder_.size() != count) {
        rebuildShuffleOrder(currentIndex);
    }

    auto recordHistory = [this, currentIndex]() {
        if (playbackHistory_.empty() || playbackHistory_.back() != currentIndex) {
            playbackHistory_.push_back(currentIndex);
            trimShuffleHistory();
        }
    };

    if (shufflePosition_ + 1 < shuffleOrder_.size()) {
        recordHistory();
        shufflePosition_++;
        return shuffleOrder_[shufflePosition_];
    }

    switch (repeatMode()) {
        case RepeatMode::One:
            return currentIndex;
        case RepeatMode::All:
            recordHistory();
            return startNewShufflePass(currentIndex);
        case RepeatMode::Off:
            return std::nullopt;
    }

    return std::nullopt;
}

std::size_t PlaybackEngine::computePreviousIndex(std::size_t currentIndex) {
    const auto count = playlist_.size();
    if (count == 0) {
        return 0;
    }

    if (shuffleMode() == ShuffleMode::Off) {
        if (currentIndex > 0) {
            return currentIndex - 1;
        }
        return repeatMode() == RepeatMode::All ? count - 1 : 0;
    }

    std::lock_guard lock(shuffleMutex_);
    if (shuffleOrder_.size() != count) {
        rebuildShuffleOrder(currentIndex);
    }

    if (!playbackHistory_.empty()) {
        const auto previousIndex = playbackHistory_.back();
        playbackHistory_.pop_back();
        shufflePosition_ = findShufflePosition(previousIndex);
        return previousIndex;
    }

    if (shufflePosition_ > 0) {
        shufflePosition_--;
        return shuffleOrder_[shufflePosition_];
    }

    if (repeatMode() == RepeatMode::All && count > 1) {
        shufflePosition_ = count - 1;
        return shuffleOrder_[shufflePosition_];
    }

    return currentIndex;
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

    if (playbackBlocked()) {
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

    const std::size_t frameBase = playbackFrame_.load();
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

    playbackFrame_.store(frameBase + framesRead);
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
