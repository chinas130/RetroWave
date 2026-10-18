// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#include "ui/Theme.h"
#include <toml++/toml.h>
#include <ncurses.h>

#include <algorithm>
#include <cmath>
#include <charconv>

namespace retrowave {

namespace {

int rgbDistSq(uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2) {
    int dr = r1 - r2;
    int dg = g1 - g2;
    int db = b1 - b2;
    return dr*dr + dg*dg + db*db;
}


short approximate256(uint8_t r, uint8_t g, uint8_t b) {
    if (COLORS >= 256) {
        if (r == g && g == b) {
            if (r < 8) return 16;
            if (r > 248) return 231;
            int step = std::round((r - 8) / 10.0);
            return 232 + std::clamp(step, 0, 23);
        }
        int ri = std::round((r < 55 ? 0 : r - 55) / 40.0);
        int gi = std::round((g < 55 ? 0 : g - 55) / 40.0);
        int bi = std::round((b < 55 ? 0 : b - 55) / 40.0);
        return 16 + 36 * std::clamp(ri, 0, 5) + 6 * std::clamp(gi, 0, 5) + std::clamp(bi, 0, 5);
    } else {
        // 16 colors fallback
        const uint8_t base16[16][3] = {
            {0,0,0}, {170,0,0}, {0,170,0}, {170,85,0}, {0,0,170}, {170,0,170}, {0,170,170}, {170,170,170},
            {85,85,85}, {255,85,85}, {85,255,85}, {255,255,85}, {85,85,255}, {255,85,255}, {85,255,255}, {255,255,255}
        };
        int best = 0;
        int minD = 1000000;
        for (int i = 0; i < 16; ++i) {
            int d = rgbDistSq(r, g, b, base16[i][0], base16[i][1], base16[i][2]);
            if (d < minD) {
                minD = d;
                best = i;
            }
        }
        return best;
    }
}

BorderStyle parseBorderStyle(std::string_view str) {
    if (str == "single") return BorderStyle::Single;
    if (str == "double") return BorderStyle::Double;
    if (str == "ascii") return BorderStyle::Ascii;
    return BorderStyle::Rounded;
}

VizStyle parseVizStyle(std::string_view str) {
    if (str == "ascii") return VizStyle::Ascii;
    if (str == "braille") return VizStyle::Braille;
    return VizStyle::Blocks;
}

} // namespace

Color parseColor(std::string_view str) {
    if (str.empty() || str == "default") {
        return Color(-1);
    }
    if (str[0] == '#') {
        if (str.length() == 7) {
            uint32_t val = 0;
            auto result = std::from_chars(str.data() + 1, str.data() + str.size(), val, 16);
            if (result.ec == std::errc()) {
                return Color((val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF);
            }
        }
        return Color(-1);
    }
    
    int16_t val = -1;
    auto result = std::from_chars(str.data(), str.data() + str.size(), val);
    if (result.ec == std::errc()) {
        return Color(val);
    }
    return Color(-1);
}

ThemeManager::ThemeManager() {
    addBuiltinThemes();
}

void ThemeManager::addBuiltinThemes() {
    Theme retrowave;
    retrowave.name = "retrowave";
    retrowave.border = parseColor("#ff00ff");
    retrowave.header_title = parseColor("#00ffff");
    retrowave.header_text = parseColor("#00ffff");
    retrowave.track_normal = Color(-1);
    retrowave.track_selected = parseColor("#000000"); // Black text for selected
    retrowave.track_playing = parseColor("#00ffff");
    retrowave.track_playing_selected = parseColor("#000000");
    retrowave.meta_title = parseColor("#ffff00");
    retrowave.meta_label = parseColor("#00ffff");
    retrowave.meta_value = Color(-1);
    retrowave.time_bar_filled = parseColor("#ff00ff");
    retrowave.time_bar_empty = parseColor("#333333");
    retrowave.time_text = parseColor("#ffff00");
    retrowave.viz_bar_low = parseColor("#00ffff");
    retrowave.viz_bar_mid = parseColor("#ff00ff");
    retrowave.viz_bar_high = parseColor("#ffff00");
    retrowave.viz_peak = parseColor("#ffffff");
    retrowave.lyric_normal = Color(-1);
    retrowave.lyric_past = parseColor("#555555");
    retrowave.lyric_active = parseColor("#00ffff");
    retrowave.lyric_active_word = parseColor("#ffff00");
    retrowave.lyric_next = parseColor("#ff00ff");
    retrowave.modal_bg = parseColor("#111111");
    retrowave.modal_border = parseColor("#00ffff");
    retrowave.modal_title = parseColor("#ffff00");
    retrowave.modal_text = Color(-1);
    
    themes_.push_back(retrowave);

    Theme mocha;
    mocha.name = "catppuccin-mocha";
    mocha.border = parseColor("#b4befe"); // lavender
    mocha.header_title = parseColor("#cba6f7"); // mauve
    mocha.header_text = parseColor("#89b4fa"); // sapphire
    mocha.track_normal = parseColor("#cdd6f4"); // text
    mocha.track_selected = parseColor("#1e1e2e"); // base
    mocha.track_playing = parseColor("#f38ba8"); // red
    mocha.track_playing_selected = parseColor("#1e1e2e"); // base
    mocha.meta_title = parseColor("#f2cdcd"); // flamingo
    mocha.meta_label = parseColor("#89b4fa"); // sapphire
    mocha.meta_value = parseColor("#cdd6f4"); // text
    mocha.time_bar_filled = parseColor("#cba6f7"); // mauve
    mocha.time_bar_empty = parseColor("#585b70"); // surface2
    mocha.time_text = parseColor("#f5e0dc"); // rosewater
    mocha.viz_bar_low = parseColor("#89b4fa"); // sapphire
    mocha.viz_bar_mid = parseColor("#cba6f7"); // mauve
    mocha.viz_bar_high = parseColor("#f38ba8"); // red
    mocha.viz_peak = parseColor("#f5c2e7"); // pink
    mocha.lyric_normal = parseColor("#cdd6f4"); // text
    mocha.lyric_past = parseColor("#6c7086"); // overlay0
    mocha.lyric_active = parseColor("#f2cdcd"); // flamingo
    mocha.lyric_active_word = parseColor("#f9e2af"); // yellow
    mocha.lyric_next = parseColor("#b4befe"); // lavender
    mocha.modal_bg = parseColor("#181825"); // mantle
    mocha.modal_border = parseColor("#f38ba8"); // red
    mocha.modal_title = parseColor("#f9e2af"); // yellow
    mocha.modal_text = parseColor("#cdd6f4"); // text
    themes_.push_back(mocha);

    Theme gruvbox;
    gruvbox.name = "gruvbox-dark";
    gruvbox.border = parseColor("#d79921"); // yellow
    gruvbox.header_title = parseColor("#b8bb26"); // green
    gruvbox.header_text = parseColor("#ebdbb2"); // fg
    gruvbox.track_normal = parseColor("#ebdbb2"); // fg
    gruvbox.track_selected = parseColor("#282828"); // bg
    gruvbox.track_playing = parseColor("#fabd2f"); // yellow
    gruvbox.track_playing_selected = parseColor("#282828"); // bg
    gruvbox.meta_title = parseColor("#fe8019"); // orange
    gruvbox.meta_label = parseColor("#b8bb26"); // green
    gruvbox.meta_value = parseColor("#ebdbb2"); // fg
    gruvbox.time_bar_filled = parseColor("#d3869b"); // purple
    gruvbox.time_bar_empty = parseColor("#665c54"); // bg3
    gruvbox.time_text = parseColor("#fabd2f"); // yellow
    gruvbox.viz_bar_low = parseColor("#b8bb26"); // green
    gruvbox.viz_bar_mid = parseColor("#fabd2f"); // yellow
    gruvbox.viz_bar_high = parseColor("#fb4934"); // red
    gruvbox.viz_peak = parseColor("#ebdbb2"); // fg
    gruvbox.lyric_normal = parseColor("#ebdbb2"); // fg
    gruvbox.lyric_past = parseColor("#928374"); // gray
    gruvbox.lyric_active = parseColor("#b8bb26"); // green
    gruvbox.lyric_active_word = parseColor("#fe8019"); // orange
    gruvbox.lyric_next = parseColor("#83a598"); // blue
    gruvbox.modal_bg = parseColor("#282828"); // bg
    gruvbox.modal_border = parseColor("#fb4934"); // red
    gruvbox.modal_title = parseColor("#fabd2f"); // yellow
    gruvbox.modal_text = parseColor("#ebdbb2"); // fg
    themes_.push_back(gruvbox);

    Theme nord;
    nord.name = "nord";
    nord.border = parseColor("#81a1c1"); // nord9
    nord.header_title = parseColor("#8fbcbb"); // nord7
    nord.header_text = parseColor("#eceff4"); // nord6
    nord.track_normal = parseColor("#d8dee9"); // nord4
    nord.track_selected = parseColor("#2e3440"); // nord0
    nord.track_playing = parseColor("#88c0d0"); // nord8
    nord.track_playing_selected = parseColor("#2e3440"); // nord0
    nord.meta_title = parseColor("#ebcb8b"); // nord13
    nord.meta_label = parseColor("#81a1c1"); // nord9
    nord.meta_value = parseColor("#d8dee9"); // nord4
    nord.time_bar_filled = parseColor("#b48ead"); // nord15
    nord.time_bar_empty = parseColor("#434c5e"); // nord2
    nord.time_text = parseColor("#eceff4"); // nord6
    nord.viz_bar_low = parseColor("#81a1c1"); // nord9
    nord.viz_bar_mid = parseColor("#88c0d0"); // nord8
    nord.viz_bar_high = parseColor("#ebcb8b"); // nord13
    nord.viz_peak = parseColor("#eceff4"); // nord6
    nord.lyric_normal = parseColor("#d8dee9"); // nord4
    nord.lyric_past = parseColor("#4c566a"); // nord3
    nord.lyric_active = parseColor("#88c0d0"); // nord8
    nord.lyric_active_word = parseColor("#ebcb8b"); // nord13
    nord.lyric_next = parseColor("#81a1c1"); // nord9
    nord.modal_bg = parseColor("#2e3440"); // nord0
    nord.modal_border = parseColor("#bf616a"); // nord11
    nord.modal_title = parseColor("#ebcb8b"); // nord13
    nord.modal_text = parseColor("#d8dee9"); // nord4
    themes_.push_back(nord);

    Theme monochrome;
    monochrome.name = "monochrome";
    monochrome.border = parseColor("#ffffff");
    monochrome.header_title = parseColor("#ffffff");
    monochrome.header_text = parseColor("#ffffff");
    monochrome.track_normal = Color(-1);
    monochrome.track_selected = parseColor("#000000");
    monochrome.track_playing = parseColor("#ffffff");
    monochrome.track_playing_selected = parseColor("#000000");
    monochrome.meta_title = parseColor("#ffffff");
    monochrome.meta_label = parseColor("#aaaaaa");
    monochrome.meta_value = Color(-1);
    monochrome.time_bar_filled = parseColor("#ffffff");
    monochrome.time_bar_empty = parseColor("#555555");
    monochrome.time_text = parseColor("#ffffff");
    monochrome.viz_bar_low = parseColor("#aaaaaa");
    monochrome.viz_bar_mid = parseColor("#cccccc");
    monochrome.viz_bar_high = parseColor("#ffffff");
    monochrome.viz_peak = parseColor("#ffffff");
    monochrome.lyric_normal = Color(-1);
    monochrome.lyric_past = parseColor("#555555");
    monochrome.lyric_active = parseColor("#ffffff");
    monochrome.lyric_active_word = parseColor("#ffffff");
    monochrome.lyric_next = parseColor("#aaaaaa");
    monochrome.modal_bg = parseColor("#000000");
    monochrome.modal_border = parseColor("#ffffff");
    monochrome.modal_title = parseColor("#ffffff");
    monochrome.modal_text = parseColor("#ffffff");
    themes_.push_back(monochrome);
}

void ThemeManager::loadCustomThemes(const std::filesystem::path& themesDir) {
    if (!std::filesystem::exists(themesDir)) return;
    for (const auto& entry : std::filesystem::directory_iterator(themesDir)) {
        if (entry.path().extension() == ".toml") {
            try {
                auto tbl = toml::parse_file(entry.path().string());
                Theme t;
                t.name = entry.path().stem().string();
                if (auto n = tbl["name"].value<std::string>()) t.name = *n;

                auto parse = [&](const char* key, Color& c) {
                    if (auto val = tbl[key].value<std::string>()) {
                        c = parseColor(*val);
                    }
                };

                parse("border", t.border);
                parse("header_title", t.header_title);
                parse("header_text", t.header_text);
                
                parse("track_normal", t.track_normal);
                parse("track_selected", t.track_selected);
                parse("track_playing", t.track_playing);
                parse("track_playing_selected", t.track_playing_selected);
                
                parse("meta_title", t.meta_title);
                parse("meta_label", t.meta_label);
                parse("meta_value", t.meta_value);
                parse("time_bar_filled", t.time_bar_filled);
                parse("time_bar_empty", t.time_bar_empty);
                parse("time_text", t.time_text);
                
                parse("viz_bar_low", t.viz_bar_low);
                parse("viz_bar_mid", t.viz_bar_mid);
                parse("viz_bar_high", t.viz_bar_high);
                parse("viz_peak", t.viz_peak);
                
                parse("lyric_normal", t.lyric_normal);
                parse("lyric_past", t.lyric_past);
                parse("lyric_active", t.lyric_active);
                parse("lyric_active_word", t.lyric_active_word);
                parse("lyric_next", t.lyric_next);
                
                parse("modal_bg", t.modal_bg);
                parse("modal_border", t.modal_border);
                parse("modal_title", t.modal_title);
                parse("modal_text", t.modal_text);

                if (auto val = tbl["border_style"].value<std::string>()) {
                    t.borderStyle = parseBorderStyle(*val);
                }
                if (auto val = tbl["viz_style"].value<std::string>()) {
                    t.vizStyle = parseVizStyle(*val);
                }

                themes_.push_back(t);
            } catch (const toml::parse_error&) {
                // Ignore parsing errors for now
            }
        }
    }
}

bool ThemeManager::selectTheme(const std::string& name) {
    for (std::size_t i = 0; i < themes_.size(); ++i) {
        if (themes_[i].name == name) {
            currentIndex_ = i;
            return true;
        }
    }
    return false;
}

void ThemeManager::nextTheme() {
    currentIndex_ = (currentIndex_ + 1) % themes_.size();
}

void ThemeManager::prevTheme() {
    if (currentIndex_ == 0) {
        currentIndex_ = themes_.size() - 1;
    } else {
        --currentIndex_;
    }
}

const Theme& ThemeManager::current() const {
    return themes_[currentIndex_];
}

short ThemeManager::allocateColor(const Color& color) {
    if (color.isTransparent) {
        return -1;
    }
    if (!color.isRgb) {
        return color.index;
    }
    if (can_change_color() && COLORS >= 256) {
        short id = nextColorId_++;
        if (nextColorId_ >= COLORS) nextColorId_ = 16;
        // ncurses init_color uses 0-1000 scale
        init_color(id, (color.r * 1000) / 255, (color.g * 1000) / 255, (color.b * 1000) / 255);
        return id;
    }
    return approximate256(color.r, color.g, color.b);
}

void ThemeManager::applyToNcurses() {
    if (!has_colors()) return;
    
    // Reset next color ID to avoid running out of custom colors over multiple applications
    nextColorId_ = 16;
    
    const Theme& t = current();
    auto initP = [&](ColorToken token, const Color& fg, const Color& bg) {
        short fgId = allocateColor(fg);
        short bgId = allocateColor(bg);
        init_pair(static_cast<short>(token), fgId, bgId);
    };

    initP(ColorToken::Border, t.border, Color(-1));
    initP(ColorToken::HeaderTitle, t.header_title, Color(-1));
    initP(ColorToken::HeaderText, t.header_text, Color(-1));
    initP(ColorToken::TrackNormal, t.track_normal, Color(-1));
    initP(ColorToken::TrackSelected, t.track_selected, Color(255, 255, 255));
    
    initP(ColorToken::TrackSelected, t.track_selected, Color(-1));
    initP(ColorToken::TrackPlaying, t.track_playing, Color(-1));
    initP(ColorToken::TrackPlayingSelected, t.track_playing_selected, Color(-1));
    
    initP(ColorToken::MetaTitle, t.meta_title, Color(-1));
    initP(ColorToken::MetaLabel, t.meta_label, Color(-1));
    initP(ColorToken::MetaValue, t.meta_value, Color(-1));
    
    initP(ColorToken::TimeBarFilled, t.time_bar_filled, Color(-1));
    initP(ColorToken::TimeBarEmpty, t.time_bar_empty, Color(-1));
    initP(ColorToken::TimeText, t.time_text, Color(-1));
    
    initP(ColorToken::VizBarLow, t.viz_bar_low, Color(-1));
    initP(ColorToken::VizBarMid, t.viz_bar_mid, Color(-1));
    initP(ColorToken::VizBarHigh, t.viz_bar_high, Color(-1));
    initP(ColorToken::VizPeak, t.viz_peak, Color(-1));
    
    initP(ColorToken::LyricNormal, t.lyric_normal, Color(-1));
    initP(ColorToken::LyricPast, t.lyric_past, Color(-1));
    initP(ColorToken::LyricActive, t.lyric_active, Color(-1));
    initP(ColorToken::LyricActiveWord, t.lyric_active_word, Color(-1));
    initP(ColorToken::LyricNext, t.lyric_next, Color(-1));
    
    initP(ColorToken::ModalBg, t.modal_text, t.modal_bg);
    initP(ColorToken::ModalBorder, t.modal_border, t.modal_bg);
    initP(ColorToken::ModalTitle, t.modal_title, t.modal_bg);
    initP(ColorToken::ModalText, t.modal_text, t.modal_bg);
}

} // namespace retrowave
