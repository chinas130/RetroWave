// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#include "core/Settings.h"
#include <toml++/toml.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::filesystem::path makeTempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / ("retrowave-settings-test-" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

}  // namespace

int main() {
    try {
        const auto configRoot = makeTempDir();
#if defined(_WIN32)
        _putenv_s("XDG_CONFIG_HOME", configRoot.string().c_str());
#else
        setenv("XDG_CONFIG_HOME", configRoot.string().c_str(), 1);
#endif

        // Write an initial config with keys
        auto configPath = configRoot / "retrowave" / "config.toml";
        std::filesystem::create_directories(configPath.parent_path());
        {
            std::ofstream out(configPath);
            out << "[general]\nvolume = 1.0\n[keys]\nquit = \"ctrl+q\"\n";
        }

        retrowave::SettingsStore store;
        auto loadedInitial = store.load();
        require(loadedInitial.keysTable["quit"].value_or<std::string_view>("") == "ctrl+q", "keys should be loaded");

        // Modify general settings and save
        loadedInitial.volume = 0.55F;
        loadedInitial.coverArtMode = retrowave::CoverArtMode::HalfBlock;
        loadedInitial.repeatMode = retrowave::RepeatMode::All;
        
        require(store.save(loadedInitial), "settings should save");

        const auto loadedFinal = store.load();
        require(loadedFinal.volume > 0.549F && loadedFinal.volume < 0.551F, "volume should round-trip");
        require(loadedFinal.coverArtMode == retrowave::CoverArtMode::HalfBlock, "cover art mode should round-trip");
        require(loadedFinal.repeatMode == retrowave::RepeatMode::All, "repeat mode should round-trip");
        require(loadedFinal.keysTable["quit"].value_or<std::string_view>("") == "ctrl+q", "keys section should be preserved");

        std::filesystem::remove_all(configRoot);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
