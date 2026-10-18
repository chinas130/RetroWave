// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#pragma once

#include <string>
#include <thread>
#include <atomic>

namespace retrowave {

class PlaybackEngine;
class TerminalUI;

class IpcServer {
public:
    IpcServer(PlaybackEngine& engine, TerminalUI* ui);
    ~IpcServer();
    
    void start();
    void stop();

    static int sendCommand(const std::string& command);

private:
    void serverLoop();
    void processCommand(int clientFd, const std::string& cmd);
    
    PlaybackEngine& engine_;
    TerminalUI* ui_;
    std::string socketPath_;
    int serverFd_{-1};
    std::atomic<bool> running_{false};
    std::thread serverThread_;
};

} // namespace retrowave
