// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#pragma once

#include <string>
#include <vector>

namespace retrowave {

class EventHooks {
  public:
    static void onTrackChange(const std::string& title,
                              const std::string& artist,
                              const std::string& album,
                              int durationSeconds,
                              const std::string& filepath);

    static void onStateChange(const std::string& state);

  private:
    static std::string getHooksDir();
    static void executeHookAsync(const std::string& hookName, const std::vector<std::string>& args);
};

} // namespace retrowave
