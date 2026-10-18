// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#include "core/EventHooks.h"
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <pwd.h>
#include <vector>
#include <thread>
#include <filesystem>

namespace retrowave {

std::string EventHooks::getHooksDir() {
    const char* xdgConfigHome = std::getenv("XDG_CONFIG_HOME");
    std::string configDir;
    if (xdgConfigHome && xdgConfigHome[0] != '\0') {
        configDir = std::string(xdgConfigHome) + "/retrowave/hooks";
    } else {
        const char* home = std::getenv("HOME");
        if (!home) {
            if (struct passwd* pw = getpwuid(getuid())) {
                home = pw->pw_dir;
            }
        }
        if (home) {
            configDir = std::string(home) + "/.config/retrowave/hooks";
        }
    }
    return configDir;
}

void EventHooks::executeHookAsync(const std::string& hookName, const std::vector<std::string>& args) {
    std::string dir = getHooksDir();
    if (dir.empty()) return;

    std::string hookPath = dir + "/" + hookName;
    if (access(hookPath.c_str(), X_OK) != 0) {
        return;
    }

    std::thread([hookPath, args]() {
        pid_t pid = fork();
        if (pid == 0) {
            std::vector<char*> c_args;
            c_args.push_back(const_cast<char*>(hookPath.c_str()));
            for (const auto& arg : args) {
                c_args.push_back(const_cast<char*>(arg.c_str()));
            }
            c_args.push_back(nullptr);

            freopen("/dev/null", "r", stdin);
            freopen("/dev/null", "w", stdout);
            freopen("/dev/null", "w", stderr);

            execv(hookPath.c_str(), c_args.data());
            _exit(127);
        } else if (pid > 0) {
            waitpid(pid, nullptr, 0);
        }
    }).detach();
}

void EventHooks::onTrackChange(const std::string& title,
                               const std::string& artist,
                               const std::string& album,
                               int durationSeconds,
                               const std::string& filepath) {
    std::vector<std::string> args = {
        title,
        artist,
        album,
        std::to_string(durationSeconds),
        filepath
    };
    executeHookAsync("on-track-change.sh", args);
}

void EventHooks::onStateChange(const std::string& state) {
    std::vector<std::string> args = { state };
    executeHookAsync("on-state-change.sh", args);
}

} // namespace retrowave
