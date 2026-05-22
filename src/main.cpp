#include "audio/PlaybackEngine.h"
#include "core/Playlist.h"
#include "ui/TerminalUI.h"

extern "C" {
#include <libavutil/log.h>
}

#include <csignal>
#include <clocale>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace {

volatile std::sig_atomic_t g_shutdownSignal = 0;

void requestShutdown(int signal) {
    g_shutdownSignal = signal;
}

void installShutdownHandlers() {
    std::signal(SIGINT, requestShutdown);
    std::signal(SIGTERM, requestShutdown);
    std::signal(SIGHUP, requestShutdown);
    std::signal(SIGQUIT, requestShutdown);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::setlocale(LC_ALL, "");
        av_log_set_level(AV_LOG_QUIET);
        installShutdownHandlers();

        std::vector<std::string> sources;
        for (int index = 1; index < argc; ++index) {
            sources.emplace_back(argv[index]);
        }

        auto playlist = retrowave::Playlist::fromSources(sources);
        retrowave::PlaybackEngine engine(std::move(playlist));
        retrowave::TerminalUI ui(engine, &g_shutdownSignal);
        const int exitCode = ui.run();
        if (g_shutdownSignal != 0) {
            return 128 + g_shutdownSignal;
        }
        return exitCode;
    } catch (const std::exception& error) {
        std::cerr << "RetroWave: " << error.what() << '\n';
        std::cerr << "Usage: retrowave [music-file-or-directory ...]\n";
        return 1;
    }
}
