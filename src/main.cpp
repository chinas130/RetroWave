// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#include "audio/PlaybackEngine.h"
#include "core/Playlist.h"
#include "ui/TerminalUI.h"
#include "core/IpcServer.h"

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

        if (argc > 1) {
            std::string arg1 = argv[1];
            if (arg1 == "play" || arg1 == "pause" || arg1 == "toggle" || arg1 == "next" || arg1 == "prev" ||
                arg1 == "stop" || arg1 == "status" || arg1 == "volume" || arg1 == "seek" || arg1 == "theme" || arg1 == "layout") {
                std::string cmd;
                for (int i = 1; i < argc; ++i) {
                    if (i > 1) cmd += " ";
                    cmd += argv[i];
                }
                return retrowave::IpcServer::sendCommand(cmd);
            }
        }

        std::vector<std::string> sources;
        for (int index = 1; index < argc; ++index) {
            sources.emplace_back(argv[index]);
        }

        auto playlist = retrowave::Playlist::fromSources(sources);
        retrowave::PlaybackEngine engine(std::move(playlist));
        retrowave::TerminalUI ui(engine, &g_shutdownSignal);
        
        retrowave::IpcServer ipcServer(engine, &ui);
        ipcServer.start();

        const int exitCode = ui.run();
        
        ipcServer.stop();

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
