#include "ui/MediaSession.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <MediaPlayer/MediaPlayer.h>

#include <mutex>

namespace retrowave {
namespace {

void enableCommand(MPRemoteCommand* command) {
    command.enabled = YES;
}

}  // namespace

struct MediaSession::Impl {
    std::function<void()> onPlayPause;
    std::function<void()> onNext;
    std::function<void()> onPrevious;

    id playPauseTarget = nil;
    id playTarget = nil;
    id pauseTarget = nil;
    id nextTarget = nil;
    id previousTarget = nil;

    std::mutex handlersMutex;
};

MediaSession::MediaSession() : impl_(std::make_unique<Impl>()) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        // Accessory keeps a low profile but still integrates with Now Playing / Touch Bar.
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

        MPRemoteCommandCenter* commandCenter = [MPRemoteCommandCenter sharedCommandCenter];
        enableCommand(commandCenter.playCommand);
        enableCommand(commandCenter.pauseCommand);
        enableCommand(commandCenter.togglePlayPauseCommand);
        enableCommand(commandCenter.nextTrackCommand);
        enableCommand(commandCenter.previousTrackCommand);

        impl_->playPauseTarget = [commandCenter.togglePlayPauseCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event) {
            (void)event;
            std::lock_guard lock(impl_->handlersMutex);
            if (impl_->onPlayPause) {
                impl_->onPlayPause();
                return MPRemoteCommandHandlerStatusSuccess;
            }
            return MPRemoteCommandHandlerStatusCommandFailed;
        }];

        impl_->playTarget = [commandCenter.playCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event) {
            (void)event;
            std::lock_guard lock(impl_->handlersMutex);
            if (impl_->onPlayPause) {
                impl_->onPlayPause();
                return MPRemoteCommandHandlerStatusSuccess;
            }
            return MPRemoteCommandHandlerStatusCommandFailed;
        }];

        impl_->pauseTarget = [commandCenter.pauseCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event) {
            (void)event;
            std::lock_guard lock(impl_->handlersMutex);
            if (impl_->onPlayPause) {
                impl_->onPlayPause();
                return MPRemoteCommandHandlerStatusSuccess;
            }
            return MPRemoteCommandHandlerStatusCommandFailed;
        }];

        impl_->nextTarget = [commandCenter.nextTrackCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event) {
            (void)event;
            std::lock_guard lock(impl_->handlersMutex);
            if (impl_->onNext) {
                impl_->onNext();
                return MPRemoteCommandHandlerStatusSuccess;
            }
            return MPRemoteCommandHandlerStatusCommandFailed;
        }];

        impl_->previousTarget = [commandCenter.previousTrackCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event) {
            (void)event;
            std::lock_guard lock(impl_->handlersMutex);
            if (impl_->onPrevious) {
                impl_->onPrevious();
                return MPRemoteCommandHandlerStatusSuccess;
            }
            return MPRemoteCommandHandlerStatusCommandFailed;
        }];
    }
}

MediaSession::~MediaSession() {
    @autoreleasepool {
        MPRemoteCommandCenter* commandCenter = [MPRemoteCommandCenter sharedCommandCenter];
        [commandCenter.togglePlayPauseCommand removeTarget:impl_->playPauseTarget];
        [commandCenter.playCommand removeTarget:impl_->playTarget];
        [commandCenter.pauseCommand removeTarget:impl_->pauseTarget];
        [commandCenter.nextTrackCommand removeTarget:impl_->nextTarget];
        [commandCenter.previousTrackCommand removeTarget:impl_->previousTarget];

        [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = nil;
        [MPNowPlayingInfoCenter defaultCenter].playbackState = MPNowPlayingPlaybackStateStopped;
    }
}

void MediaSession::setOnPlayPause(std::function<void()> handler) {
    std::lock_guard lock(impl_->handlersMutex);
    impl_->onPlayPause = std::move(handler);
}

void MediaSession::setOnNext(std::function<void()> handler) {
    std::lock_guard lock(impl_->handlersMutex);
    impl_->onNext = std::move(handler);
}

void MediaSession::setOnPrevious(std::function<void()> handler) {
    std::lock_guard lock(impl_->handlersMutex);
    impl_->onPrevious = std::move(handler);
}

void MediaSession::updateNowPlaying(
    const std::string& title,
    const std::string& artist,
    const std::string& album,
    double durationSeconds,
    double positionSeconds) {
    @autoreleasepool {
        NSMutableDictionary* nowPlayingInfo = [NSMutableDictionary dictionary];

        if (!title.empty()) {
            nowPlayingInfo[MPMediaItemPropertyTitle] = [NSString stringWithUTF8String:title.c_str()];
        } else {
            nowPlayingInfo[MPMediaItemPropertyTitle] = @"RetroWave";
        }
        if (!artist.empty()) {
            nowPlayingInfo[MPMediaItemPropertyArtist] = [NSString stringWithUTF8String:artist.c_str()];
        }
        if (!album.empty()) {
            nowPlayingInfo[MPMediaItemPropertyAlbumTitle] = [NSString stringWithUTF8String:album.c_str()];
        }

        nowPlayingInfo[MPNowPlayingInfoPropertyMediaType] = @(MPMediaTypeMusic);

        if (durationSeconds > 0.0) {
            nowPlayingInfo[MPMediaItemPropertyPlaybackDuration] = @(durationSeconds);
            nowPlayingInfo[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(positionSeconds);
        }

        MPNowPlayingInfoCenter* center = [MPNowPlayingInfoCenter defaultCenter];
        center.nowPlayingInfo = nowPlayingInfo;
    }
}

void MediaSession::updatePlaybackState(bool isPlaying) {
    @autoreleasepool {
        MPNowPlayingInfoCenter* center = [MPNowPlayingInfoCenter defaultCenter];
        center.playbackState = isPlaying ? MPNowPlayingPlaybackStatePlaying : MPNowPlayingPlaybackStatePaused;

        NSMutableDictionary* info = [center.nowPlayingInfo mutableCopy];
        if (info == nil) {
            info = [NSMutableDictionary dictionary];
        }
        info[MPNowPlayingInfoPropertyPlaybackRate] = isPlaying ? @(1.0) : @(0.0);
        center.nowPlayingInfo = info;
    }
}

void MediaSession::pumpEvents() {
    @autoreleasepool {
        const CFTimeInterval interval = 0.01;
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, interval, true);
        CFRunLoopRunInMode((CFStringRef)NSRunLoopCommonModes, interval, true);

        while (true) {
            NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                    untilDate:[NSDate distantPast]
                                                       inMode:NSDefaultRunLoopMode
                                                      dequeue:YES];
            if (event == nil) {
                break;
            }
            [NSApp sendEvent:event];
        }
    }
}

}  // namespace retrowave
