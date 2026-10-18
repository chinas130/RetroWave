// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#pragma once

#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <string>

namespace toml {
inline namespace v3 {
class table;
}
}

namespace retrowave {

enum class Action {
    Quit,
    TogglePause,
    NextTrack,
    PrevTrack,
    VolumeUp,
    VolumeDown,
    SeekForwardShort,
    SeekBackwardShort,
    SeekForwardLong,
    SeekBackwardLong,
    ToggleDetailMode,
    CycleCoverMode,
    CycleRepeatMode,
    CycleShuffleMode,
    OpenSettings,
    OpenWarranty,
    OpenConditions,
    DismissModal,
    PlaySelected,
    CycleLayoutPreset,
    None
};

class KeyBinder {
public:
    KeyBinder();

    Action resolve(int ncursesKey) const noexcept;
    void bind(int ncursesKey, Action action);
    void loadDefaults();
    static std::optional<int> parseKey(std::string_view keyName);
    void loadFromToml(const toml::table& keysTable);

private:
    std::unordered_map<int, Action> bindings_;
};

} // namespace retrowave
