// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#pragma once

#include "core/Settings.h"
#include "ui/LayoutEngine.h"
#include "ui/Theme.h"
#include <string>

namespace retrowave {

class ModalsView {
public:
    static void drawModalOverlay(int rows, int cols, const std::string& title, const std::string& subtitle, const std::string& body);
    static void drawLegalScreen(int rows, int cols, const std::string& title, const std::string& subtitle, const std::string& body);
    static void drawSettingsOverlay(int rows, int cols, int selectedIndex, const AppSettings& settings, LayoutPreset layoutPreset, const Theme& theme);
};

} // namespace retrowave
