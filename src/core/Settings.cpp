#include "core/Settings.h"

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
            continue;
        }

        if (key == "repeat_mode") {
            settings.repeatMode = parseRepeatMode(value);
            continue;
        }

        if (key == "shuffle_mode") {
            settings.shuffleMode = parseShuffleMode(value);
        }
    }

    return settings;
}

bool SettingsStore::save(const AppSettings& settings) const {
    const auto path = settingsPath();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);

    std::ostringstream content;
    content << "volume=" << clampVolume(settings.volume) << '\n';
    content << "cover_art_mode=" << coverArtModeName(settings.coverArtMode) << '\n';
    content << "repeat_mode=" << repeatModeName(settings.repeatMode) << '\n';
    content << "shuffle_mode=" << shuffleModeName(settings.shuffleMode) << '\n';

    const auto temporaryPath = path.string() + ".tmp";
    {
        std::ofstream output(temporaryPath, std::ios::trunc);
        if (!output.is_open()) {
            return false;
        }

        output << content.str();
        if (!output.good()) {
            std::filesystem::remove(temporaryPath, error);
            return false;
        }
    }

    std::filesystem::rename(temporaryPath, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporaryPath, path, error);
    }

    if (error) {
        std::filesystem::remove(temporaryPath, error);
        return false;
    }

    return true;
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
