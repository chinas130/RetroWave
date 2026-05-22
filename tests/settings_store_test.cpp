#include "core/Settings.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
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

        retrowave::AppSettings saved;
        saved.volume = 0.55F;
        saved.coverArtMode = retrowave::CoverArtMode::HalfBlock;
        saved.repeatMode = retrowave::RepeatMode::All;

        retrowave::SettingsStore store;
        require(store.save(saved), "settings should save");

        const auto loaded = store.load();
        require(loaded.volume > 0.549F && loaded.volume < 0.551F, "volume should round-trip");
        require(loaded.coverArtMode == retrowave::CoverArtMode::HalfBlock, "cover art mode should round-trip");
        require(loaded.repeatMode == retrowave::RepeatMode::All, "repeat mode should round-trip");

        std::filesystem::remove_all(configRoot);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
