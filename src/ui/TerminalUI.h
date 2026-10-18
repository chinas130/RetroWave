// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#pragma once

#include "audio/PlaybackEngine.h"
#include "core/Settings.h"
#include "ui/MediaSession.h"
#include "ui/KeyBinder.h"
#include "ui/Theme.h"
#include "ui/LayoutEngine.h"
#include "ui/views/VisualizerView.h"
#include "ui/views/LyricsView.h"

#include <chrono>
#include <csignal>
#include <limits>
#include <string>

namespace retrowave {

class TerminalUI {
  public:
    void cycleTheme(int delta);
    void setTheme(const std::string& name);
    void setLayoutPreset(const std::string& preset);
    void cycleLayoutPreset(int delta = 1);
    explicit TerminalUI(PlaybackEngine& engine, const volatile std::sig_atomic_t* shutdownSignal = nullptr);
    ~TerminalUI();

    int run();

  private:
    void draw(const PlaybackSnapshot& snapshot, std::chrono::steady_clock::time_point now);
    [[nodiscard]] int computePollTimeout(
        const PlaybackSnapshot& snapshot,
        std::chrono::steady_clock::time_point now) const;
    void clearRect(const Rect& rect) const;
    void drawHeader(const Rect& rect, const PlaybackSnapshot& snapshot) const;
    void drawFrame(int top, int left, int height, int width, const char* title) const;
    void drawPlaylist(int top, int left, int height, int width, const PlaybackSnapshot& snapshot) const;
    void drawAlbumCard(int top, int left, int height, int width, const PlaybackSnapshot& snapshot) const;
    void drawCoverPane(int top, int left, int height, int width, const PlaybackSnapshot& snapshot) const;
    void drawMetaPane(int top, int left, int height, int width, const PlaybackSnapshot& snapshot) const;
    void drawTimePane(int top, int left, int height, int width, const PlaybackSnapshot& snapshot) const;
    void handleInput(int key);
    void syncErrorOverlay(const PlaybackSnapshot& snapshot);
    void openWarrantyOverlay();
    void openConditionsOverlay();
    void openSettingsOverlay();
    void closeTransientOverlay();
    void cycleCoverArtMode(int delta);
    void cycleRepeatMode();
    void cycleShuffleMode();
    [[nodiscard]] bool shutdownRequested() const noexcept;
    void flushSettingsFromEngine();
    void persistSettings();
    void syncMediaSession(const PlaybackSnapshot& snapshot);

    PlaybackEngine& engine_;
    const volatile std::sig_atomic_t* shutdownSignal_ = nullptr;
    SettingsStore settingsStore_;
    AppSettings settings_;
    ThemeManager themeManager_;
    std::size_t selectedIndex_ = 0;
    bool running_ = true;
    std::string activeError_;
    std::string dismissedError_;
    std::string modalTitle_;
    std::string modalSubtitle_;
    std::string modalBody_;
    bool settingsOpen_ = false;
    int settingsSelectedIndex_ = 0;
    
    DetailMode detailMode_ = DetailMode::Visualizer;
    LayoutPreset layoutPreset_ = LayoutPreset::Default;
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
    PlaybackState lastPlaybackState_ = PlaybackState::Idle;
    bool lastLyricsFound_ = false;
    int lastVolumePercent_ = -1;
    RepeatMode lastRepeatMode_ = RepeatMode::Off;
    ShuffleMode lastShuffleMode_ = ShuffleMode::Off;
    int lastPositionSecond_ = std::numeric_limits<int>::min();
    int lastActiveLyricIndex_ = std::numeric_limits<int>::min();
    DetailMode lastDetailMode_ = DetailMode::Visualizer;
    std::chrono::steady_clock::time_point lastVisualizerRedraw_{};
    std::string lastMediaPath_;
    PlaybackState lastMediaState_ = PlaybackState::Idle;
    int lastMediaPositionSecond_ = std::numeric_limits<int>::min();
    
    MediaSession mediaSession_;
    KeyBinder keyBinder_;

    VisualizerView visualizerView_;
    LyricsView lyricsView_;
};

}  // namespace retrowave
