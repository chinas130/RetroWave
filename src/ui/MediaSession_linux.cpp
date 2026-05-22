#include "ui/MediaSession.h"

namespace retrowave {

struct MediaSession::Impl {
};

MediaSession::MediaSession() : impl_(std::make_unique<Impl>()) {
}

MediaSession::~MediaSession() = default;

void MediaSession::setOnPlayPause(std::function<void()> /* handler */) {
}

void MediaSession::setOnNext(std::function<void()> /* handler */) {
}

void MediaSession::setOnPrevious(std::function<void()> /* handler */) {
}

void MediaSession::updateNowPlaying(
    const std::string& /* title */,
    const std::string& /* artist */,
    const std::string& /* album */,
    double /* durationSeconds */,
    double /* positionSeconds */) {
}

void MediaSession::updatePlaybackState(bool /* isPlaying */) {
}

void MediaSession::pumpEvents() {
}

}  // namespace retrowave
