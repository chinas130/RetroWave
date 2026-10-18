// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#pragma once

#include "core/PlaybackMode.h"
#include <toml++/toml.h>
#include <filesystem>
#include <string>

namespace retrowave {

enum class CoverArtMode {
    Ascii,
    BlockShading,
    HalfBlock
};

struct AppSettings {
    float volume = 0.85F;
    CoverArtMode coverArtMode = CoverArtMode::Ascii;
    RepeatMode repeatMode = RepeatMode::Off;
    ShuffleMode shuffleMode = ShuffleMode::Off;
    std::string theme = "retrowave";
    std::string layoutPreset = "default";
    toml::table keysTable;
};

class SettingsStore {
  public:
    [[nodiscard]] AppSettings load() const;
    bool save(const AppSettings& settings) const;

  private:
    [[nodiscard]] std::filesystem::path settingsPath() const;
};

}  // namespace retrowave
