#include "audio/AudioOutput.h"

#include <alsa/asoundlib.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace retrowave {
namespace {

constexpr int kChannelCount = 2;

void checkAlsa(int status, const char* message) {
    if (status < 0) {
        throw std::runtime_error(std::string(message) + ": " + snd_strerror(status));
    }
}

}  // namespace

struct AudioOutput::Impl {
    explicit Impl(RenderCallback render) : render_(std::move(render)) {
        checkAlsa(snd_pcm_open(&pcm_, "default", SND_PCM_STREAM_PLAYBACK, 0), "Unable to open ALSA device");

        const unsigned int latencyUs = static_cast<unsigned int>(
            (static_cast<double>(AudioOutput::queuedLatencyFrames()) / AudioOutput::sampleRate()) * 1'000'000.0);

        checkAlsa(
            snd_pcm_set_params(
                pcm_,
                SND_PCM_FORMAT_S16_LE,
                SND_PCM_ACCESS_RW_INTERLEAVED,
                kChannelCount,
                static_cast<unsigned int>(AudioOutput::sampleRate()),
                1,
                std::max(20'000U, latencyUs)),
            "Unable to configure ALSA device");
    }

    ~Impl() {
        stop();
        if (pcm_ != nullptr) {
            snd_pcm_close(pcm_);
            pcm_ = nullptr;
        }
    }

    void start() {
        if (started_) {
            return;
        }

        stopRequested_.store(false);
        worker_ = std::thread([this]() { renderLoop(); });
        started_ = true;
    }

    void stop() {
        stopRequested_.store(true);
        if (worker_.joinable()) {
            worker_.join();
        }
        started_ = false;
    }

    void renderLoop() {
        try {
            std::vector<std::int16_t> samples(AudioOutput::framesPerBuffer() * static_cast<std::size_t>(kChannelCount), 0);

            while (!stopRequested_.load()) {
                const std::size_t framesWritten = render_(samples.data(), AudioOutput::framesPerBuffer());
                if (framesWritten < AudioOutput::framesPerBuffer()) {
                    std::fill(
                        samples.begin() + static_cast<std::ptrdiff_t>(framesWritten * kChannelCount),
                        samples.end(),
                        0);
                }

                std::size_t frameOffset = 0;
                while (frameOffset < AudioOutput::framesPerBuffer() && !stopRequested_.load()) {
                    const snd_pcm_sframes_t writeResult = snd_pcm_writei(
                        pcm_,
                        samples.data() + static_cast<std::ptrdiff_t>(frameOffset * kChannelCount),
                        AudioOutput::framesPerBuffer() - frameOffset);

                    if (writeResult > 0) {
                        frameOffset += static_cast<std::size_t>(writeResult);
                        continue;
                    }

                    if (writeResult == -EPIPE) {
                        snd_pcm_prepare(pcm_);
                        continue;
                    }

                    if (writeResult == -ESTRPIPE) {
                        while ((snd_pcm_resume(pcm_)) == -EAGAIN && !stopRequested_.load()) {
                            std::this_thread::yield();
                        }
                        snd_pcm_prepare(pcm_);
                        continue;
                    }

                    if (writeResult == -EINTR) {
                        continue;
                    }

                    throw std::runtime_error("ALSA write failed: " + std::string(snd_strerror(static_cast<int>(writeResult))));
                }
            }

            snd_pcm_drop(pcm_);
            snd_pcm_prepare(pcm_);
        } catch (...) {
            stopRequested_.store(true);
        }
    }

    RenderCallback render_;
    snd_pcm_t* pcm_ = nullptr;
    std::thread worker_;
    std::atomic<bool> stopRequested_{false};
    bool started_ = false;
};

AudioOutput::AudioOutput(RenderCallback render)
    : impl_(std::make_unique<Impl>(std::move(render))) {}

AudioOutput::~AudioOutput() = default;

void AudioOutput::start() {
    impl_->start();
}

void AudioOutput::stop() {
    impl_->stop();
}

}  // namespace retrowave
