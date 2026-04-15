#pragma once

#include <filesystem>

namespace retrowave {

enum class CoverArtMode {
    Ascii,
    BlockShading,
    HalfBlock
};

struct AppSettings {
    float volume = 0.85F;
    CoverArtMode coverArtMode = CoverArtMode::Ascii;
};

class SettingsStore {
  public:
    [[nodiscard]] AppSettings load() const;
    bool save(const AppSettings& settings) const;

  private:
    [[nodiscard]] std::filesystem::path settingsPath() const;
};

}  // namespace retrowave
