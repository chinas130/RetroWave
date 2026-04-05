#pragma once

#include "audio/AudioDecoder.h"
#include "audio/AudioRingBuffer.h"
#include "audio/WaveformCache.h"
#include "core/Playlist.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace retrowave {

class AudioOutput;
class AudioStreamDecoder;
inline constexpr std::size_t kPlaybackVisualizerBins = 64;

struct PlaybackSnapshot {
    bool hasTrack = false;
    bool paused = false;
    bool loading = false;
    std::size_t currentIndex = 0;
    float volume = 0.85F;
    float level = 0.0F;
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
    bool playIndex(std::size_t index);
    void togglePause();
    void next();
    void previous();
    void adjustVolume(float delta);

    [[nodiscard]] PlaybackSnapshot snapshot() const;
    [[nodiscard]] const Playlist& playlist() const noexcept;

  private:
    void stopDecoderThread();
    void decoderLoop();
    std::size_t renderFrames(std::int16_t* destination, std::size_t frames);
    bool playIndexInternal(std::size_t index);
    void publishVisualizer(const std::array<float, kPlaybackVisualizerBins>& bins) noexcept;

    Playlist playlist_;
    AudioDecoder decoder_;
    WaveformCache waveformCache_;
    std::unique_ptr<AudioOutput> output_;
    std::unique_ptr<AudioStreamDecoder> streamDecoder_;
    AudioRingBuffer ringBuffer_;
    std::thread decoderThread_;

    mutable std::mutex mutex_;
    std::optional<DecodedTrack> currentTrack_;
    std::size_t currentIndex_ = 0;
    std::string lastError_;
    std::string audioError_;
    std::array<std::array<float, kPlaybackVisualizerBins>, 2> visualizerBuffers_{};
    std::atomic<int> activeVisualizerBuffer_{0};

    std::atomic<std::size_t> playbackFrame_{0};
    std::atomic<bool> stopDecoder_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> loading_{false};
    std::atomic<bool> pendingAdvance_{false};
    std::atomic<bool> trackEnded_{false};
    std::atomic<float> volume_{0.85F};
    std::atomic<float> level_{0.0F};
};

}  // namespace retrowave
