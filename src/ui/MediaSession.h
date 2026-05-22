#pragma once

#include <functional>
#include <memory>
#include <string>

namespace retrowave {

class MediaSession {
  public:
    MediaSession();
    ~MediaSession();

    MediaSession(const MediaSession&) = delete;
    MediaSession& operator=(const MediaSession&) = delete;

    void setOnPlayPause(std::function<void()> handler);
    void setOnNext(std::function<void()> handler);
    void setOnPrevious(std::function<void()> handler);

    void updateNowPlaying(
        const std::string& title,
        const std::string& artist,
        const std::string& album,
        double durationSeconds,
        double positionSeconds);

    void updatePlaybackState(bool isPlaying);

    void pumpEvents();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace retrowave
