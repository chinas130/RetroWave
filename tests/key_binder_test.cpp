// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#include "ui/KeyBinder.h"
#include <toml++/toml.h>
#include <ncurses.h>

#include <cassert>
#include <iostream>

using namespace retrowave;

void testDefaultBindings() {
    KeyBinder binder;
    
    assert(binder.resolve('q') == Action::Quit);
    assert(binder.resolve('Q') == Action::Quit);
    assert(binder.resolve(' ') == Action::TogglePause);
    assert(binder.resolve(KEY_UP) == Action::PrevTrack);
    assert(binder.resolve(KEY_DOWN) == Action::NextTrack);
    assert(binder.resolve('\n') == Action::PlaySelected);
    assert(binder.resolve(27) == Action::DismissModal);
    assert(binder.resolve('c') == Action::CycleCoverMode);
    assert(binder.resolve('C') == Action::CycleCoverMode);
}

void testParseKey() {
    assert(KeyBinder::parseKey("q") == 'q');
    assert(KeyBinder::parseKey("Q") == 'q');
    assert(KeyBinder::parseKey("space") == ' ');
    assert(KeyBinder::parseKey("enter") == '\n');
    assert(KeyBinder::parseKey("esc") == 27);
    assert(KeyBinder::parseKey("tab") == '\t');
    assert(KeyBinder::parseKey("backspace") == KEY_BACKSPACE);
    assert(KeyBinder::parseKey("up") == KEY_UP);
    assert(KeyBinder::parseKey("down") == KEY_DOWN);
    assert(KeyBinder::parseKey("left") == KEY_LEFT);
    assert(KeyBinder::parseKey("right") == KEY_RIGHT);
    assert(KeyBinder::parseKey("page_up") == KEY_PPAGE);
    assert(KeyBinder::parseKey("page_down") == KEY_NPAGE);
    
    assert(KeyBinder::parseKey("ctrl+c") == ('c' & 0x1f));
    assert(KeyBinder::parseKey("ctrl+X") == ('x' & 0x1f));
}

void testTomlLoad() {
    auto tomlData = toml::parse(R"(
        quit = ["q", "ctrl+c", "esc"]
        next = "j"
        prev = ["k", "up"]
    )");
    
    KeyBinder binder;
    binder.loadFromToml(*tomlData.as_table());
    
    assert(binder.resolve('q') == Action::Quit);
    assert(binder.resolve('Q') == Action::Quit);
    assert(binder.resolve('c' & 0x1f) == Action::Quit);
    assert(binder.resolve(27) == Action::Quit);
    
    assert(binder.resolve('j') == Action::NextTrack);
    assert(binder.resolve('J') == Action::NextTrack);
    assert(binder.resolve('k') == Action::PrevTrack);
    assert(binder.resolve(KEY_UP) == Action::PrevTrack);
    
    // check defaults are preserved for unmentioned keys
    assert(binder.resolve(' ') == Action::TogglePause);
}

int main() {
    testDefaultBindings();
    testParseKey();
    testTomlLoad();
    std::cout << "KeyBinder tests passed!" << std::endl;
    return 0;
}
