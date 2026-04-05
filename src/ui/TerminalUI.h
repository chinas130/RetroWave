#pragma once

#include "audio/PlaybackEngine.h"

#include <chrono>
#include <limits>
#include <string>
#include <vector>

namespace retrowave {

enum class DetailMode {
    Visualizer,
    Lyrics
};

class TerminalUI {
  public:
    explicit TerminalUI(PlaybackEngine& engine);
    int run();

  private:
    struct Rect {
        int top = 0;
        int left = 0;
        int height = 0;
        int width = 0;
    };

    struct Layout {
        int rows = 0;
        int cols = 0;
        Rect header;
        Rect playlistFrame;
        Rect playlistContent;
        Rect albumFrame;
        Rect cover;
        Rect meta;
        Rect time;
        Rect detailFrame;
        Rect detailContent;
    };

    void draw(const PlaybackSnapshot& snapshot, std::chrono::steady_clock::time_point now);
    [[nodiscard]] Layout computeLayout(int rows, int cols) const;
    [[nodiscard]] int computePollTimeout(
        const PlaybackSnapshot& snapshot,
        std::chrono::steady_clock::time_point now) const;
    void clearRect(const Rect& rect) const;
    void drawHeader(const Rect& rect) const;
    void drawFrame(int top, int left, int height, int width, const char* title) const;
    void drawPlaylist(int top, int left, int height, int width, const PlaybackSnapshot& snapshot) const;
    void drawAlbumCard(int top, int left, int height, int width, const PlaybackSnapshot& snapshot) const;
    void drawCoverPane(int top, int left, int height, int width, const PlaybackSnapshot& snapshot) const;
    void drawMetaPane(int top, int left, int height, int width, const PlaybackSnapshot& snapshot) const;
    void drawTimePane(int top, int left, int height, int width, const PlaybackSnapshot& snapshot) const;
    void drawVisualizer(int top, int left, int height, int width, const PlaybackSnapshot& snapshot) const;
    void drawLyrics(int top, int left, int height, int width, const PlaybackSnapshot& snapshot) const;
    void drawModalOverlay(int rows, int cols, const std::string& title, const std::string& subtitle, const std::string& body) const;
    void drawLegalScreen(int rows, int cols) const;
    void handleInput(int key);
    void syncErrorOverlay(const PlaybackSnapshot& snapshot);
    void openWarrantyOverlay();
    void openConditionsOverlay();

    PlaybackEngine& engine_;
    std::size_t selectedIndex_ = 0;
    bool running_ = true;
    std::string activeError_;
    std::string dismissedError_;
    std::string modalTitle_;
    std::string modalSubtitle_;
    std::string modalBody_;
    mutable std::vector<float> visualizerState_;
    mutable std::size_t visualizerTick_ = 0;
    DetailMode detailMode_ = DetailMode::Visualizer;
    Layout layout_;
    bool layoutValid_ = false;
    bool needsFullRedraw_ = true;
    bool overlayVisibleLastFrame_ = false;
    std::size_t lastSelectedIndex_ = std::numeric_limits<std::size_t>::max();
    std::size_t lastCurrentIndex_ = std::numeric_limits<std::size_t>::max();
    std::string lastTrackPath_;
    std::string lastTitle_;
    std::string lastArtist_;
    std::string lastAlbum_;
    std::string lastErrorText_;
    bool lastHasTrack_ = false;
    bool lastPaused_ = false;
    bool lastLoading_ = false;
    bool lastLyricsFound_ = false;
    int lastVolumePercent_ = -1;
    int lastPositionSecond_ = std::numeric_limits<int>::min();
    int lastActiveLyricIndex_ = std::numeric_limits<int>::min();
    DetailMode lastDetailMode_ = DetailMode::Visualizer;
    std::chrono::steady_clock::time_point lastVisualizerRedraw_{};
};

}  // namespace retrowave
