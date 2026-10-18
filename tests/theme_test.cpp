// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#include "ui/Theme.h"
#include <cassert>
#include <iostream>
#include <fstream>

using namespace retrowave;

void testColorParsing() {
    Color c1 = parseColor("#ff0000");
    assert(c1.isRgb && c1.r == 255 && c1.g == 0 && c1.b == 0);

    Color c2 = parseColor("12");
    assert(!c2.isRgb && c2.index == 12);

    Color c3 = parseColor("default");
    assert(c3.isTransparent && !c3.isRgb && c3.index == -1);

    Color c4 = parseColor("");
    assert(c4.isTransparent);
    
    Color c5 = parseColor("#abcdef");
    assert(c5.isRgb && c5.r == 0xab && c5.g == 0xcd && c5.b == 0xef);
}

void testBuiltinThemes() {
    ThemeManager tm;
    assert(tm.selectTheme("retrowave"));
    assert(tm.selectTheme("catppuccin-mocha"));
    assert(tm.selectTheme("gruvbox-dark"));
    assert(tm.selectTheme("nord"));
    assert(tm.selectTheme("monochrome"));
    assert(!tm.selectTheme("nonexistent"));
}

void testCustomTheme() {
    std::filesystem::path dir = "test_themes";
    std::filesystem::create_directories(dir);

    std::ofstream out(dir / "mytheme.toml");
    out << "name = \"mytheme\"\n"
        << "border_style = \"double\"\n"
        << "viz_style = \"braille\"\n"
        << "border = \"#112233\"\n"
        << "track_selected = \"20\"\n";
    out.close();

    ThemeManager tm;
    tm.loadCustomThemes(dir);
    assert(tm.selectTheme("mytheme"));
    
    const Theme& t = tm.current();
    assert(t.name == "mytheme");
    assert(t.borderStyle == BorderStyle::Double);
    assert(t.vizStyle == VizStyle::Braille);
    assert(t.border.isRgb && t.border.r == 0x11 && t.border.g == 0x22 && t.border.b == 0x33);
    assert(!t.track_selected.isRgb && t.track_selected.index == 20);

    std::filesystem::remove_all(dir);
}

int main() {
    testColorParsing();
    testBuiltinThemes();
    testCustomTheme();
    std::cout << "All theme tests passed!" << std::endl;
    return 0;
}
