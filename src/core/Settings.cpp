#include "core/Settings.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
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

}  // namespace

AppSettings SettingsStore::load() const {
    AppSettings settings;

    std::ifstream input(settingsPath());
    if (!input.is_open()) {
        return settings;
    }

    std::string line;
    while (std::getline(input, line)) {
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        const auto key = line.substr(0, separator);
        const auto value = line.substr(separator + 1);

        if (key == "volume") {
            try {
                settings.volume = clampVolume(std::stof(value));
            } catch (...) {
            }
            continue;
        }

        if (key == "cover_art_mode") {
            settings.coverArtMode = parseCoverArtMode(value);
        }
    }

    return settings;
}

bool SettingsStore::save(const AppSettings& settings) const {
    const auto path = settingsPath();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);

    std::ofstream output(path, std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }

    output << "volume=" << clampVolume(settings.volume) << '\n';
    output << "cover_art_mode=" << coverArtModeName(settings.coverArtMode) << '\n';
    return output.good();
}

std::filesystem::path SettingsStore::settingsPath() const {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && xdg[0] != '\0') {
        return std::filesystem::path(xdg) / "retrowave" / "settings.conf";
    }

    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        return std::filesystem::path(home) / ".config" / "retrowave" / "settings.conf";
    }

    return std::filesystem::current_path() / ".retrowave-settings.conf";
}

}  // namespace retrowave
