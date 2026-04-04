#pragma once

#include "audio/PlaybackEngine.h"

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
    void draw(const PlaybackSnapshot& snapshot) const;
    void drawFrame(int top, int left, int height, int width, const char* title) const;
    void drawPlaylist(int top, int left, int height, int width, const PlaybackSnapshot& snapshot) const;
    void drawAlbumCard(int top, int left, int height, int width, const PlaybackSnapshot& snapshot) const;
    void drawVisualizer(int top, int left, int height, int width, const PlaybackSnapshot& snapshot) const;
    void drawLyrics(int top, int left, int height, int width, const PlaybackSnapshot& snapshot) const;
    void drawErrorOverlay(int rows, int cols) const;
    void handleInput(int key);
    void syncErrorOverlay(const PlaybackSnapshot& snapshot);

    PlaybackEngine& engine_;
    std::size_t selectedIndex_ = 0;
    bool running_ = true;
    std::string activeError_;
    std::string dismissedError_;
    mutable std::vector<float> visualizerState_;
    mutable std::size_t visualizerTick_ = 0;
    DetailMode detailMode_ = DetailMode::Visualizer;
};

}  // namespace retrowave
