// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#include "core/IpcServer.h"
#include "audio/PlaybackEngine.h"
#include "core/Playlist.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <cassert>

int main() {
    retrowave::Playlist playlist;
    retrowave::PlaybackEngine engine(std::move(playlist));
    retrowave::IpcServer server(engine, nullptr);
    
    std::cout << "Starting server..." << std::endl;
    server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    std::cout << "Sending status..." << std::endl;
    int ret1 = retrowave::IpcServer::sendCommand("status");
    
    std::cout << "Sending play..." << std::endl;
    int ret2 = retrowave::IpcServer::sendCommand("play");

    std::cout << "Sending volume +5..." << std::endl;
    int ret3 = retrowave::IpcServer::sendCommand("volume +5");
    
    std::cout << "Sending invalid command..." << std::endl;
    int ret4 = retrowave::IpcServer::sendCommand("invalid_cmd");
    
    server.stop();
    
    if (ret1 == 0 && ret2 == 0 && ret3 == 0 && ret4 == 1) {
        std::cout << "IPC test passed" << std::endl;
        return 0;
    }
    
    std::cerr << "IPC test failed" << std::endl;
    return 1;
}
