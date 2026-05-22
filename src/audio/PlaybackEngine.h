#pragma once

#include "audio/AudioDecoder.h"
#include "audio/AudioRingBuffer.h"
#include "audio/MetadataCache.h"
#include "audio/WaveformCache.h"
#include "core/PlaybackMode.h"
#include "core/Playlist.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace retrowave {

class AudioOutput;
class AudioStreamDecoder;
inline constexpr std::size_t kPlaybackVisualizerBins = 64;

enum class PlaybackState {
    Idle,
    Loading,
    Playing,
    Paused,
    Buffering,
    Ended,
    Failed,
};

struct PlaybackSnapshot {
    bool hasTrack = false;
    bool paused = false;
    bool loading = false;
    PlaybackState state = PlaybackState::Idle;
    bool remote = false;
    bool live = false;
    std::size_t currentIndex = 0;
    float volume = 0.85F;
    float level = 0.0F;
    RepeatMode repeatMode = RepeatMode::Off;
    ShuffleMode shuffleMode = ShuffleMode::Off;
    double positionSeconds = 0.0;
    double durationSeconds = 0.0;
    std::string title;
    std::string artist;
    std::string album;
    std::string path;
    std::string lastError;
    std::vector<float> waveform;
    std::vector<float> visualizer;
    std::shared_ptr<const AlbumArt> albumArt;
    std::shared_ptr<const LyricsData> lyrics;
};

class PlaybackEngine {
  public:
    explicit PlaybackEngine(Playlist playlist);
    ~PlaybackEngine();

    PlaybackEngine(const PlaybackEngine&) = delete;
    PlaybackEngine& operator=(const PlaybackEngine&) = delete;

    void update();
    void ensureLyrics();
    bool playIndex(std::size_t index);
    void togglePause();
    void next();
    void previous();
    void adjustVolume(float delta);
    void setVolume(float value);
    void setRepeatMode(RepeatMode mode);
    [[nodiscard]] RepeatMode repeatMode() const noexcept;
    void setShuffleMode(ShuffleMode mode);
    [[nodiscard]] ShuffleMode shuffleMode() const noexcept;

    [[nodiscard]] PlaybackSnapshot snapshot() const;
    [[nodiscard]] const Playlist& playlist() const noexcept;

  private:
    void stopDecoderThread();
    [[nodiscard]] std::size_t navigationBaseIndex() const;
    [[nodiscard]] PlaybackState state() const noexcept;
    void setState(PlaybackState state) noexcept;
    [[nodiscard]] bool playbackBlocked() const noexcept;
    [[nodiscard]] std::optional<std::size_t> computeNextIndex(std::size_t currentIndex);
    [[nodiscard]] std::size_t computePreviousIndex(std::size_t currentIndex);
    void clearShuffleHistory();
    void trimShuffleHistory();
    void syncShufflePosition(std::size_t index);
    void rebuildShuffleOrder(std::size_t anchorIndex);
    [[nodiscard]] std::size_t findShufflePosition(std::size_t index) const;
    [[nodiscard]] std::size_t startNewShufflePass(std::size_t avoidIndex);
    void loaderLoop();
    [[nodiscard]] bool loadStillCurrent(std::uint64_t generation) const noexcept;
    void loadIndex(std::size_t index, std::uint64_t generation);
    void decoderLoop();
    std::size_t renderFrames(std::int16_t* destination, std::size_t frames);
    bool playIndexInternal(std::size_t index);
    void publishVisualizer(const std::array<float, kPlaybackVisualizerBins>& bins) noexcept;
    void finalizeWaveformSampling();
    void sampleWaveformFrame(std::size_t absoluteFrame, float peak) noexcept;

    Playlist playlist_;
    AudioDecoder decoder_;
    MetadataCache metadataCache_;
    WaveformCache waveformCache_;
    std::unique_ptr<AudioOutput> output_;
    std::unique_ptr<AudioStreamDecoder> streamDecoder_;
    AudioRingBuffer ringBuffer_;
    std::thread loaderThread_;
    std::thread decoderThread_;

    mutable std::mutex mutex_;
    mutable std::mutex loaderMutex_;
    std::condition_variable loaderCv_;
    std::optional<DecodedTrack> currentTrack_;
    std::size_t requestedLoadIndex_ = 0;
    std::uint64_t requestedLoadGeneration_ = 0;
    bool loadRequested_ = false;
    std::string lastError_;
    std::string audioError_;
    std::array<std::array<float, kPlaybackVisualizerBins>, 2> visualizerBuffers_{};
    std::atomic<int> activeVisualizerBuffer_{0};

    std::atomic<std::size_t> playbackFrame_{0};
    std::atomic<std::size_t> currentIndex_{0};
    std::atomic<std::size_t> loadingIndex_{0};
    std::atomic<std::uint64_t> loadGeneration_{0};
    std::atomic<bool> stopDecoder_{false};
    std::atomic<bool> stopLoader_{false};
    std::atomic<PlaybackState> state_{PlaybackState::Idle};
    std::atomic<bool> currentRemote_{false};
    std::atomic<bool> currentLive_{false};
    std::atomic<bool> pendingAdvance_{false};
    std::atomic<bool> trackEnded_{false};
    std::atomic<float> volume_{0.85F};
    std::atomic<float> level_{0.0F};
    std::atomic<RepeatMode> repeatMode_{RepeatMode::Off};
    std::atomic<ShuffleMode> shuffleMode_{ShuffleMode::Off};

    mutable std::mutex shuffleMutex_;
    std::vector<std::size_t> shuffleOrder_;
    std::size_t shufflePosition_ = 0;
    std::vector<std::size_t> playbackHistory_;
    static constexpr std::size_t kMaxPlaybackHistory = 256;

    std::filesystem::path samplingPath_;
    std::array<float, kWaveformBinCount> samplingBins_{};
    std::size_t samplingTotalFrames_ = 0;
    bool samplingActive_ = false;
};

}  // namespace retrowave
