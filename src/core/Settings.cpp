// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#include "core/Settings.h"
#include <toml++/toml.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace retrowave {
namespace {

float clampVolume(float volume) {
    return std::max(0.0F, std::min(volume, 1.2F));
}

CoverArtMode parseCoverArtMode(const std::string& value) {
    if (value == "block") {
        return CoverArtMode::BlockShading;
    }
    if (value == "half-block") {
        return CoverArtMode::HalfBlock;
    }
    return CoverArtMode::Ascii;
}

RepeatMode parseRepeatMode(const std::string& value) {
    if (value == "one") {
        return RepeatMode::One;
    }
    if (value == "all") {
        return RepeatMode::All;
    }
    return RepeatMode::Off;
}

ShuffleMode parseShuffleMode(const std::string& value) {
    if (value == "on") {
        return ShuffleMode::On;
    }
    return ShuffleMode::Off;
}

const char* coverArtModeName(CoverArtMode mode) {
    switch (mode) {
        case CoverArtMode::Ascii:
            return "ascii";
        case CoverArtMode::BlockShading:
            return "block";
        case CoverArtMode::HalfBlock:
            return "half-block";
    }

    return "ascii";
}

const char* repeatModeName(RepeatMode mode) {
    switch (mode) {
        case RepeatMode::Off:
            return "off";
        case RepeatMode::One:
            return "one";
        case RepeatMode::All:
            return "all";
    }

    return "off";
}

const char* shuffleModeName(ShuffleMode mode) {
    switch (mode) {
        case ShuffleMode::Off:
            return "off";
        case ShuffleMode::On:
            return "on";
    }

    return "off";
}

}  // namespace

AppSettings SettingsStore::load() const {
    AppSettings settings;
    try {
        toml::table tbl = toml::parse_file(settingsPath().string());
        if (auto general = tbl["general"].as_table()) {
            if (auto vol = general->get("volume")) {
                if (vol->is_number()) {
                    settings.volume = clampVolume(static_cast<float>(vol->value_or(0.85)));
                }
            }
            if (auto cam = general->get("cover_art_mode")) {
                if (cam->is_string()) {
                    settings.coverArtMode = parseCoverArtMode(cam->value_or("ascii"));
                }
            }
            if (auto rm = general->get("repeat_mode")) {
                if (rm->is_string()) {
                    settings.repeatMode = parseRepeatMode(rm->value_or("off"));
                }
            }
            if (auto sm = general->get("shuffle_mode")) {
                if (sm->is_string()) {
                    settings.shuffleMode = parseShuffleMode(sm->value_or("off"));
                }
            }
            if (auto th = general->get("theme")) {
                if (th->is_string()) {
                    settings.theme = th->value_or("retrowave");
                }
            }
            if (auto lp = general->get("layout_preset")) {
                if (lp->is_string()) {
                    settings.layoutPreset = lp->value_or("default");
                }
            }
        }
        if (auto keys = tbl["keys"].as_table()) {
            settings.keysTable = *keys;
        }
    } catch (const toml::parse_error&) {
        // Just return defaults if parsing fails or file doesn't exist
    }

    return settings;
}

bool SettingsStore::save(const AppSettings& settings) const {
    const auto path = settingsPath();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);

    toml::table tbl;
    try {
        tbl = toml::parse_file(path.string());
    } catch (...) {
        // File might not exist or be invalid, start fresh
    }

    if (!tbl["general"].is_table()) {
        tbl.insert_or_assign("general", toml::table{});
    }

    auto* general = tbl["general"].as_table();
    general->insert_or_assign("volume", clampVolume(settings.volume));
    general->insert_or_assign("cover_art_mode", coverArtModeName(settings.coverArtMode));
    general->insert_or_assign("repeat_mode", repeatModeName(settings.repeatMode));
    general->insert_or_assign("shuffle_mode", shuffleModeName(settings.shuffleMode));
    general->insert_or_assign("theme", settings.theme);
    general->insert_or_assign("layout_preset", settings.layoutPreset);

    const auto temporaryPath = path.string() + ".tmp";
    {
        std::ofstream output(temporaryPath, std::ios::trunc);
        if (!output.is_open()) {
            return false;
        }

        output << tbl;
        if (!output.good()) {
            std::filesystem::remove(temporaryPath, error);
            return false;
        }
        output.flush();
    }

    std::filesystem::rename(temporaryPath, path, error);
    if (error) {
        std::filesystem::remove(temporaryPath, error);
        return false;
    }

    return true;
}

std::filesystem::path SettingsStore::settingsPath() const {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && xdg[0] != '\0') {
        return std::filesystem::path(xdg) / "retrowave" / "config.toml";
    }

    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        return std::filesystem::path(home) / ".config" / "retrowave" / "config.toml";
    }

    return std::filesystem::current_path() / ".retrowave-config.toml";
}

}  // namespace retrowave
