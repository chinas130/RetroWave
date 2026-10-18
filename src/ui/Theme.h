// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <cstdint>
#include <map>

namespace retrowave {

struct Color {
    int16_t index = -1;
    uint8_t r = 0, g = 0, b = 0;
    bool isRgb = false;
    bool isTransparent = true;

    Color() = default;
    Color(int16_t i) : index(i), isRgb(false), isTransparent(i == -1) {}
    Color(uint8_t red, uint8_t green, uint8_t blue) : r(red), g(green), b(blue), isRgb(true), isTransparent(false) {}
};

Color parseColor(std::string_view str);

enum class BorderStyle {
    Single,
    Rounded,
    Double,
    Ascii
};

enum class VizStyle {
    Blocks,
    Ascii,
    Braille
};

struct Theme {
    std::string name;

    // Borders & Headers
    Color border;
    Color header_title;
    Color header_text;

    // Playlist
    Color track_normal;
    Color track_selected;
    Color track_playing;
    Color track_playing_selected;

    // Album Card & Time
    Color meta_title;
    Color meta_label;
    Color meta_value;
    Color time_bar_filled;
    Color time_bar_empty;
    Color time_text;

    // Visualizer
    Color viz_bar_low;
    Color viz_bar_mid;
    Color viz_bar_high;
    Color viz_peak;

    // Lyrics
    Color lyric_normal;
    Color lyric_past;
    Color lyric_active;
    Color lyric_active_word;
    Color lyric_next;

    // Overlays / Modals
    Color modal_bg;
    Color modal_border;
    Color modal_title;
    Color modal_text;

    BorderStyle borderStyle = BorderStyle::Rounded;
    VizStyle vizStyle = VizStyle::Blocks;
};

enum class ColorToken : short {
    Border = 1,
    HeaderTitle,
    HeaderText,
    TrackNormal,
    TrackSelected,
    TrackPlaying,
    TrackPlayingSelected,
    MetaTitle,
    MetaLabel,
    MetaValue,
    TimeBarFilled,
    TimeBarEmpty,
    TimeText,
    VizBarLow,
    VizBarMid,
    VizBarHigh,
    VizPeak,
    LyricNormal,
    LyricPast,
    LyricActive,
    LyricActiveWord,
    LyricNext,
    ModalBg,
    ModalBorder,
    ModalTitle,
    ModalText,
    MAX_TOKEN
};

class ThemeManager {
  public:
    ThemeManager();

    void loadCustomThemes(const std::filesystem::path& themesDir);
    bool selectTheme(const std::string& name);
    void applyToNcurses();

    void nextTheme();
    void prevTheme();
    const Theme& current() const;

  private:
    std::vector<Theme> themes_;
    std::size_t currentIndex_ = 0;
    short nextColorId_ = 16; // Start custom colors after basic 16

    void addBuiltinThemes();
    short allocateColor(const Color& color);
};

}  // namespace retrowave
