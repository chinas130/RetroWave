// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#include "ui/KeyBinder.h"

#include <ncurses.h>
#include <toml++/toml.h>
#include <cctype>
#include <algorithm>

namespace retrowave {

KeyBinder::KeyBinder() {
    loadDefaults();
}

Action KeyBinder::resolve(int ncursesKey) const noexcept {
    auto it = bindings_.find(ncursesKey);
    if (it != bindings_.end()) {
        return it->second;
    }
    return Action::None;
}

void KeyBinder::bind(int ncursesKey, Action action) {
    bindings_[ncursesKey] = action;
}

void KeyBinder::loadDefaults() {
    bindings_.clear();
    bind('q', Action::Quit);
    bind('Q', Action::Quit);
    bind(' ', Action::TogglePause);
    bind('n', Action::NextTrack);
    bind('N', Action::NextTrack);
    bind('p', Action::PrevTrack);
    bind('P', Action::PrevTrack);
    bind('+', Action::VolumeUp);
    bind('=', Action::VolumeUp);
    bind('-', Action::VolumeDown);
    bind('_', Action::VolumeDown);
    bind('<', Action::SeekBackwardShort);
    bind(',', Action::SeekBackwardShort);
    bind('>', Action::SeekForwardShort);
    bind('.', Action::SeekForwardShort);
    bind('r', Action::CycleRepeatMode);
    bind('R', Action::CycleRepeatMode);
    bind('t', Action::ToggleDetailMode);
    bind('T', Action::ToggleDetailMode);
    bind('s', Action::OpenSettings);
    bind('S', Action::OpenSettings);
    bind('w', Action::OpenWarranty);
    bind('W', Action::OpenWarranty);
    bind('c', Action::CycleCoverMode);
    bind('C', Action::CycleCoverMode);
    bind('h', Action::CycleShuffleMode);
    bind('H', Action::CycleShuffleMode);
    bind(KEY_PPAGE, Action::SeekBackwardLong);
    bind(KEY_NPAGE, Action::SeekForwardLong);
    bind(KEY_UP, Action::PrevTrack);
    bind(KEY_DOWN, Action::NextTrack);
    bind(KEY_LEFT, Action::SeekBackwardShort);
    bind(KEY_RIGHT, Action::SeekForwardShort);
    bind('\n', Action::PlaySelected);
    bind(KEY_ENTER, Action::PlaySelected);
    bind('\t', Action::CycleLayoutPreset); // Tab
    bind(27, Action::DismissModal); // Esc
}

std::optional<int> KeyBinder::parseKey(std::string_view keyName) {
    if (keyName.empty()) {
        return std::nullopt;
    }

    std::string lowerKey;
    for (char c : keyName) {
        lowerKey += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (lowerKey.size() == 1) {
        return static_cast<int>(lowerKey[0]);
    }

    if (lowerKey == "space") return ' ';
    if (lowerKey == "enter" || lowerKey == "return") return '\n';
    if (lowerKey == "esc" || lowerKey == "escape") return 27;
    if (lowerKey == "tab") return '\t';
    if (lowerKey == "backspace") return KEY_BACKSPACE;

    if (lowerKey == "up") return KEY_UP;
    if (lowerKey == "down") return KEY_DOWN;
    if (lowerKey == "left") return KEY_LEFT;
    if (lowerKey == "right") return KEY_RIGHT;
    if (lowerKey == "page_up") return KEY_PPAGE;
    if (lowerKey == "page_down") return KEY_NPAGE;

    if (lowerKey.starts_with("ctrl+")) {
        std::string_view remainder = keyName.substr(5);
        if (remainder.size() == 1) {
            char c = static_cast<char>(std::tolower(static_cast<unsigned char>(remainder[0])));
            if (c >= 'a' && c <= 'z') {
                return c & 0x1f;
            }
        }
    }

    return std::nullopt;
}

void KeyBinder::loadFromToml(const toml::table& keysTable) {
    auto parseAndBind = [this](const toml::node& node, Action action) {
        if (const auto* str = node.as_string()) {
            if (auto key = parseKey(str->get())) {
                bind(*key, action);
                // Auto-bind uppercase if it's a lowercase letter
                if (*key >= 'a' && *key <= 'z') {
                    bind(std::toupper(*key), action);
                }
            }
        } else if (const auto* arr = node.as_array()) {
            for (const auto& el : *arr) {
                if (const auto* str = el.as_string()) {
                    if (auto key = parseKey(str->get())) {
                        bind(*key, action);
                        if (*key >= 'a' && *key <= 'z') {
                            bind(std::toupper(*key), action);
                        }
                    }
                }
            }
        }
    };

    struct Mapping {
        std::string_view name;
        Action action;
    };

    const Mapping mappings[] = {
        {"quit", Action::Quit},
        {"toggle_pause", Action::TogglePause},
        {"next", Action::NextTrack},
        {"prev", Action::PrevTrack},
        {"volume_up", Action::VolumeUp},
        {"volume_down", Action::VolumeDown},
        {"seek_forward_short", Action::SeekForwardShort},
        {"seek_backward_short", Action::SeekBackwardShort},
        {"seek_forward_long", Action::SeekForwardLong},
        {"seek_backward_long", Action::SeekBackwardLong},
        {"toggle_detail_mode", Action::ToggleDetailMode},
        {"cycle_cover_mode", Action::CycleCoverMode},
        {"cycle_repeat_mode", Action::CycleRepeatMode},
        {"cycle_shuffle_mode", Action::CycleShuffleMode},
        {"open_settings", Action::OpenSettings},
        {"open_warranty", Action::OpenWarranty},
        {"open_conditions", Action::OpenConditions},
        {"dismiss_modal", Action::DismissModal},
        {"cycle_layout", Action::CycleLayoutPreset},
    };

    for (const auto& mapping : mappings) {
        if (auto node = keysTable.get(mapping.name)) {
            parseAndBind(*node, mapping.action);
        }
    }
}

} // namespace retrowave
