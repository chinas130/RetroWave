// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#include "core/IpcServer.h"
#include "audio/PlaybackEngine.h"
#include "ui/TerminalUI.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <vector>
#include <cstring>
#include <cstdlib>

namespace retrowave {

static std::string getSocketPath() {
    const char* xdgRuntimeDir = std::getenv("XDG_RUNTIME_DIR");
    if (xdgRuntimeDir && xdgRuntimeDir[0] != '\0') {
        return std::string(xdgRuntimeDir) + "/retrowave.sock";
    }
    return "/tmp/retrowave-" + std::to_string(getuid()) + ".sock";
}

IpcServer::IpcServer(PlaybackEngine& engine, TerminalUI* ui)
    : engine_(engine), ui_(ui), socketPath_(getSocketPath()) {
}

IpcServer::~IpcServer() {
    stop();
}

void IpcServer::start() {
    if (running_) return;

    serverFd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (serverFd_ < 0) {
        std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
        return;
    }

    if (access(socketPath_.c_str(), F_OK) == 0) {
        int testFd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socketPath_.c_str(), sizeof(addr.sun_path) - 1);

        if (connect(testFd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            std::cerr << "Another instance is running" << std::endl;
            close(testFd);
            close(serverFd_);
            serverFd_ = -1;
            return;
        }
        close(testFd);
        unlink(socketPath_.c_str());
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath_.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(serverFd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind socket: " << strerror(errno) << std::endl;
        close(serverFd_);
        serverFd_ = -1;
        return;
    }

    if (chmod(socketPath_.c_str(), 0600) < 0) {
        std::cerr << "Failed to set socket permissions: " << strerror(errno) << std::endl;
        close(serverFd_);
        serverFd_ = -1;
        unlink(socketPath_.c_str());
        return;
    }

    if (listen(serverFd_, 5) < 0) {
        std::cerr << "Failed to listen on socket: " << strerror(errno) << std::endl;
        close(serverFd_);
        serverFd_ = -1;
        unlink(socketPath_.c_str());
        return;
    }

    int flags = fcntl(serverFd_, F_GETFL, 0);
    fcntl(serverFd_, F_SETFL, flags | O_NONBLOCK);

    running_ = true;
    serverThread_ = std::thread(&IpcServer::serverLoop, this);
}

void IpcServer::stop() {
    if (!running_) return;
    running_ = false;
    if (serverThread_.joinable()) {
        serverThread_.join();
    }
    if (serverFd_ >= 0) {
        close(serverFd_);
        serverFd_ = -1;
    }
    unlink(socketPath_.c_str());
}

struct ClientContext {
    int fd;
    std::string buffer;
};

void IpcServer::serverLoop() {
    std::vector<pollfd> pfds;
    std::vector<ClientContext> clients;

    pfds.push_back({serverFd_, POLLIN, 0});

    while (running_) {
        int ret = poll(pfds.data(), pfds.size(), 100);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) continue; // timeout

        // Check server fd
        if (pfds[0].revents & POLLIN) {
            int clientFd = accept(serverFd_, nullptr, nullptr);
            if (clientFd >= 0) {
                int flags = fcntl(clientFd, F_GETFL, 0);
                fcntl(clientFd, F_SETFL, flags | O_NONBLOCK);

                pfds.push_back({clientFd, POLLIN, 0});
                clients.push_back({clientFd, ""});
            }
        }

        // Check clients (iterate backwards to allow safe removal)
        for (size_t i = pfds.size() - 1; i > 0; --i) {
            bool removeClient = false;
            if (pfds[i].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) {
                char buf[1024];
                ssize_t n = read(pfds[i].fd, buf, sizeof(buf));
                if (n > 0) {
                    auto& client = clients[i - 1];
                    client.buffer.append(buf, n);

                    if (client.buffer.size() > 4096) {
                        std::cerr << "Client exceeded buffer limit, closing." << std::endl;
                        removeClient = true;
                    } else {
                        size_t pos;
                        while ((pos = client.buffer.find('\n')) != std::string::npos) {
                            std::string cmd = client.buffer.substr(0, pos);
                            client.buffer.erase(0, pos + 1);
                            if (!cmd.empty() && cmd.back() == '\r') {
                                cmd.pop_back();
                            }
                            processCommand(pfds[i].fd, cmd);
                        }
                    }
                } else if (n == 0) {
                    removeClient = true; // EOF
                } else {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        removeClient = true;
                    }
                }
            }

            if (removeClient) {
                close(pfds[i].fd);
                pfds.erase(pfds.begin() + i);
                clients.erase(clients.begin() + i - 1);
            }
        }
    }

    // Clean up remaining clients
    for (size_t i = 1; i < pfds.size(); ++i) {
        close(pfds[i].fd);
    }
}

static std::string escapeJsonString(const std::string& input) {
    std::ostringstream ss;
    for (char c : input) {
        switch (c) {
            case '\"': ss << "\\\""; break;
            case '\\': ss << "\\\\"; break;
            case '\b': ss << "\\b"; break;
            case '\f': ss << "\\f"; break;
            case '\n': ss << "\\n"; break;
            case '\r': ss << "\\r"; break;
            case '\t': ss << "\\t"; break;
            default:
                if ('\x00' <= c && c <= '\x1f') {
                    ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                } else {
                    ss << c;
                }
        }
    }
    return ss.str();
}

static std::vector<std::string> tokenizeCommand(const std::string& str) {
    std::vector<std::string> tokens;
    size_t start = 0;
    while (start < str.length()) {
        while (start < str.length() && std::isspace(static_cast<unsigned char>(str[start]))) {
            start++;
        }
        if (start >= str.length()) break;
        size_t end = start;
        while (end < str.length() && !std::isspace(static_cast<unsigned char>(str[end]))) {
            end++;
        }
        tokens.push_back(str.substr(start, end - start));
        start = end;
    }
    return tokens;
}

static bool parseFloatSafe(const std::string& str, float& out) {
    char* end = nullptr;
    const char* c_str = str.c_str();
    errno = 0;
    out = std::strtof(c_str, &end);
    if (errno == ERANGE) return false;
    if (end == c_str) return false;
    if (*end != '\0') return false; 
    return true;
}

static bool parseDoubleSafe(const std::string& str, double& out) {
    char* end = nullptr;
    const char* c_str = str.c_str();
    errno = 0;
    out = std::strtod(c_str, &end);
    if (errno == ERANGE) return false;
    if (end == c_str) return false;
    if (*end != '\0') return false;
    return true;
}

void IpcServer::processCommand(int clientFd, const std::string& cmd) {
    auto sendResponse = [clientFd](const std::string& response) {
        ssize_t totalWritten = 0;
        while (totalWritten < (ssize_t)response.size()) {
            ssize_t n = write(clientFd, response.c_str() + totalWritten, response.size() - totalWritten);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    struct pollfd pfd{};
                    pfd.fd = clientFd;
                    pfd.events = POLLOUT;
                    poll(&pfd, 1, 100);
                    continue;
                }
                break;
            }
            totalWritten += n;
        }
        shutdown(clientFd, SHUT_WR);
    };

    auto sendSuccess = [&sendResponse](const std::string& msg) {
        sendResponse("{\"ok\":true,\"message\":\"" + msg + "\"}\n");
    };
    auto sendVolume = [&sendResponse](int vol) {
        sendResponse("{\"ok\":true,\"volume\":" + std::to_string(vol) + "}\n");
    };
    auto sendError = [&sendResponse](const std::string& err) {
        sendResponse("{\"ok\":false,\"error\":\"" + err + "\"}\n");
    };

    try {
        std::vector<std::string> tokens = tokenizeCommand(cmd);
        if (tokens.empty()) {
            sendError("Empty command");
            return;
        }

        std::string action = tokens[0];
        std::transform(action.begin(), action.end(), action.begin(), ::tolower);

        if (action == "play") {
            auto snap = engine_.snapshot();
            if (snap.state == PlaybackState::Paused) {
                engine_.togglePause();
            } else if (snap.state == PlaybackState::Idle) {
                engine_.playIndex(snap.currentIndex);
            }
            sendSuccess("Playback started");
        } else if (action == "pause") {
            if (engine_.snapshot().state == PlaybackState::Playing) {
                engine_.togglePause();
            }
            sendSuccess("Playback paused");
        } else if (action == "toggle") {
            if (engine_.snapshot().state == PlaybackState::Idle) {
                engine_.playIndex(engine_.snapshot().currentIndex);
            } else {
                engine_.togglePause();
            }
            sendSuccess("Playback toggled");
        } else if (action == "next") {
            engine_.next();
            sendSuccess("Skipped to next track");
        } else if (action == "prev") {
            engine_.previous();
            sendSuccess("Skipped to previous track");
        } else if (action == "stop") {
            engine_.stop();
            sendSuccess("Playback stopped");
        } else if (action == "volume") {
            if (tokens.size() < 2) {
                sendError("Missing volume argument");
                return;
            }
            std::string val = tokens[1];
            if (val == "up") {
                engine_.adjustVolume(0.05f);
                sendVolume(static_cast<int>(engine_.snapshot().volume * 100.0f));
            } else if (val == "down") {
                engine_.adjustVolume(-0.05f);
                sendVolume(static_cast<int>(engine_.snapshot().volume * 100.0f));
            } else {
                float v;
                if (!parseFloatSafe(val, v)) {
                    sendError("Invalid volume argument: " + val);
                    return;
                }
                if (val[0] == '+' || val[0] == '-') {
                    engine_.adjustVolume(v / 100.0f);
                } else {
                    engine_.setVolume(v / 100.0f);
                }
                sendVolume(static_cast<int>(engine_.snapshot().volume * 100.0f));
            }
        } else if (action == "seek") {
            if (tokens.size() < 2) {
                sendError("Missing seek argument");
                return;
            }
            std::string val = tokens[1];
            double v;
            if (!parseDoubleSafe(val, v)) {
                sendError("Invalid seek argument: " + val);
                return;
            }
            auto snap = engine_.snapshot();
            if (val[0] == '+' || val[0] == '-') {
                engine_.seek(snap.positionSeconds + v);
            } else {
                engine_.seek(v);
            }
            sendSuccess("Seek successful");
        } else if (action == "status") {
            auto snap = engine_.snapshot();
            std::ostringstream ss;
            ss << "{";
            ss << "\"ok\":true,";
            ss << "\"state\":\"";
            switch (snap.state) {
                case PlaybackState::Playing: ss << "playing"; break;
                case PlaybackState::Paused: ss << "paused"; break;
                case PlaybackState::Idle: ss << "stopped"; break;
                default: ss << "stopped"; break;
            }
            ss << "\",\"title\":\"" << escapeJsonString(snap.title) << "\"";
            ss << ",\"artist\":\"" << escapeJsonString(snap.artist) << "\"";
            ss << ",\"album\":\"" << escapeJsonString(snap.album) << "\"";
            ss << ",\"position\":" << snap.positionSeconds;
            ss << ",\"duration\":" << snap.durationSeconds;
            ss << ",\"volume\":" << static_cast<int>(snap.volume * 100.0f);
            ss << "}\n";

            sendResponse(ss.str());
        } else if (action == "theme") {
            if (tokens.size() < 2) {
                sendError("Missing theme name");
                return;
            }
            if (ui_) {
                if (tokens[1] == "next") {
                    ui_->cycleTheme(1);
                } else {
                    ui_->setTheme(tokens[1]);
                }
            }
            sendSuccess("Theme changed");
        } else if (action == "layout") {
            if (tokens.size() < 2) {
                sendError("Missing layout preset");
                return;
            }
            if (ui_) {
                if (tokens[1] == "next") {
                    ui_->cycleLayoutPreset(1);
                } else {
                    ui_->setLayoutPreset(tokens[1]);
                }
            }
            sendSuccess("Layout changed");
        } else {
            sendError("Unknown command: " + action);
        }
    } catch (const std::exception& e) {
        sendError(std::string("Exception in processCommand: ") + e.what());
    } catch (...) {
        sendError("Unknown exception in processCommand");
    }
}

int IpcServer::sendCommand(const std::string& command) {
    std::string path = getSocketPath();
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return 1;

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        if (errno != EINPROGRESS) {
            std::cerr << "connect failed: " << strerror(errno) << std::endl;
            close(fd);
            std::cerr << "RetroWave instance is not running" << std::endl;
            return 1;
        }
    }

    struct pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLOUT;

    if (poll(&pfd, 1, 1000) <= 0) {
        std::cerr << "Connection timeout" << std::endl;
        close(fd);
        return 1;
    }

    int error = 0;
    socklen_t len = sizeof(error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
        std::cerr << "RetroWave instance is not running" << std::endl;
        close(fd);
        return 1;
    }

    std::string cmd = command + "\n";
    size_t totalWritten = 0;
    while (totalWritten < cmd.size()) {
        pfd.events = POLLOUT;
        if (poll(&pfd, 1, 1000) <= 0) {
            std::cerr << "Write timeout" << std::endl;
            close(fd);
            return 1;
        }
        ssize_t n = write(fd, cmd.c_str() + totalWritten, cmd.size() - totalWritten);
        if (n < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                close(fd);
                return 1;
            }
        } else {
            totalWritten += n;
        }
    }

    char buf[1024];
    std::string response;
    while (true) {
        pfd.events = POLLIN;
        if (poll(&pfd, 1, 1000) <= 0) {
            break;
        }
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            response += buf;
        } else if (n == 0) {
            break; // Server closed connection after writing
        } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                break;
            }
        }
    }

    if (!response.empty()) {
        bool isOkFalse = response.find("\"ok\":false") != std::string::npos || response.find("\"ok\": false") != std::string::npos;
        if (isOkFalse) {
            std::string errStr = "Error from server";
            size_t errPos = response.find("\"error\":\"");
            if (errPos != std::string::npos) {
                errPos += 9;
                size_t endPos = response.find("\"", errPos);
                if (endPos != std::string::npos) {
                    errStr = response.substr(errPos, endPos - errPos);
                }
            }
            std::cerr << errStr << std::endl;
            close(fd);
            return 1;
        } else {
            std::cout << response;
        }
    }

    close(fd);
    return 0;
}

} // namespace retrowave
