// SPDX-License-Identifier: GPL-3.0-or-later
// RetroWave - Copyright (C) 2026 Viktor Voloshko

#include "ui/views/ModalsView.h"
#include "ui/views/ViewUtils.h"
#include <ncurses.h>
#include <algorithm>
#include <cmath>
#include <memory>

namespace retrowave {

namespace {

struct WindowDeleter {
    void operator()(WINDOW* win) const {
        if (win) {
            delwin(win);
        }
    }
};

using ScopedWindow = std::unique_ptr<WINDOW, WindowDeleter>;

void drawFrameLocal(int top, int left, int height, int width, const char* title, const Theme& theme) {
    if (width <= 0 || height <= 0) return;
    if (has_colors()) {
        attron(COLOR_PAIR(static_cast<int>(ColorToken::Border)));
    }

    if (theme.borderStyle == BorderStyle::Ascii) {
        mvaddch(top, left, '+');
        mvhline(top, left + 1, '-', width - 2);
        mvaddch(top, left + width - 1, '+');
        mvvline(top + 1, left, '|', height - 2);
        mvvline(top + 1, left + width - 1, '|', height - 2);
        mvaddch(top + height - 1, left, '+');
        mvhline(top + height - 1, left + 1, '-', width - 2);
        mvaddch(top + height - 1, left + width - 1, '+');
    } else if (theme.borderStyle == BorderStyle::Rounded) {
        mvprintw(top, left, "╭");
        for (int i = 1; i < width - 1; ++i) mvprintw(top, left + i, "─");
        mvprintw(top, left + width - 1, "╮");
        for (int i = 1; i < height - 1; ++i) {
            mvprintw(top + i, left, "│");
            mvprintw(top + i, left + width - 1, "│");
        }
        mvprintw(top + height - 1, left, "╰");
        for (int i = 1; i < width - 1; ++i) mvprintw(top + height - 1, left + i, "─");
        mvprintw(top + height - 1, left + width - 1, "╯");
    } else if (theme.borderStyle == BorderStyle::Double) {
        mvprintw(top, left, "╔");
        for (int i = 1; i < width - 1; ++i) mvprintw(top, left + i, "═");
        mvprintw(top, left + width - 1, "╗");
        for (int i = 1; i < height - 1; ++i) {
            mvprintw(top + i, left, "║");
            mvprintw(top + i, left + width - 1, "║");
        }
        mvprintw(top + height - 1, left, "╚");
        for (int i = 1; i < width - 1; ++i) mvprintw(top + height - 1, left + i, "═");
        mvprintw(top + height - 1, left + width - 1, "╝");
    } else {
        mvaddch(top, left, ACS_ULCORNER);
        mvhline(top, left + 1, ACS_HLINE, width - 2);
        mvaddch(top, left + width - 1, ACS_URCORNER);
        mvvline(top + 1, left, ACS_VLINE, height - 2);
        mvvline(top + 1, left + width - 1, ACS_VLINE, height - 2);
        mvaddch(top + height - 1, left, ACS_LLCORNER);
        mvhline(top + height - 1, left + 1, ACS_HLINE, width - 2);
        mvaddch(top + height - 1, left + width - 1, ACS_LRCORNER);
    }

    if (has_colors()) {
        attroff(COLOR_PAIR(static_cast<int>(ColorToken::Border)));
        attron(COLOR_PAIR(static_cast<int>(ColorToken::HeaderTitle)) | A_BOLD);
    } else {
        attron(A_BOLD);
    }
    
    mvprintw(top, left + 2, " %s ", title);
    
    if (has_colors()) {
        attroff(COLOR_PAIR(static_cast<int>(ColorToken::HeaderTitle)) | A_BOLD);
    } else {
        attroff(A_BOLD);
    }
}

}

void ModalsView::drawModalOverlay(int rows, int cols, const std::string& title, const std::string& subtitle, const std::string& body) {
    const int overlayWidth = std::min(cols - 8, std::max(52, cols * 2 / 3));
    const auto wrappedBody = wrapText(body, overlayWidth - 6);
    const int messageLines = std::min<int>(wrappedBody.size(), std::max(3, rows - 14));
    const int overlayHeight = std::min(rows - 6, 6 + messageLines);
    const int top = std::max(2, (rows - overlayHeight) / 2);
    const int left = std::max(2, (cols - overlayWidth) / 2);

    if (has_colors()) {
        attron(COLOR_PAIR(3) | A_DIM);
    } else {
        attron(A_DIM);
    }
    for (int row = 1; row < rows - 1; ++row) {
        mvhline(row, 1, ' ', cols - 2);
    }
    if (has_colors()) {
        attroff(COLOR_PAIR(3) | A_DIM);
    } else {
        attroff(A_DIM);
    }

    ScopedWindow overlay(newwin(overlayHeight, overlayWidth, top, left));
    if (!overlay) {
        return;
    }

    if (has_colors()) {
        wbkgd(overlay.get(), COLOR_PAIR(static_cast<int>(ColorToken::ModalBg)) | ' ');
    } else {
        wbkgd(overlay.get(), A_REVERSE);
    }
    werase(overlay.get());
    
    if (has_colors()) wattron(overlay.get(), COLOR_PAIR(static_cast<int>(ColorToken::ModalBorder)));
    box(overlay.get(), 0, 0);
    if (has_colors()) wattroff(overlay.get(), COLOR_PAIR(static_cast<int>(ColorToken::ModalBorder)));

    wattron(overlay.get(), A_BOLD);
    if (has_colors()) wattron(overlay.get(), COLOR_PAIR(static_cast<int>(ColorToken::ModalTitle)));
    mvwprintw(overlay.get(), 0, 2, " %s ", title.c_str());
    if (has_colors()) wattroff(overlay.get(), COLOR_PAIR(static_cast<int>(ColorToken::ModalTitle)));
    
    if (has_colors()) wattron(overlay.get(), COLOR_PAIR(static_cast<int>(ColorToken::ModalText)));
    mvwaddnstr(overlay.get(), 1, 2, trimText(subtitle, overlayWidth - 4).c_str(), -1);
    wattroff(overlay.get(), A_BOLD);

    for (int index = 0; index < messageLines; ++index) {
        mvwhline(overlay.get(), 2 + index, 2, ' ', overlayWidth - 4);
        mvwaddnstr(
            overlay.get(),
            2 + index,
            2,
            trimText(wrappedBody[static_cast<std::size_t>(index)], overlayWidth - 4).c_str(),
            -1);
    }

    if (static_cast<int>(wrappedBody.size()) > messageLines) {
        mvwprintw(overlay.get(), 2 + messageLines - 1, overlayWidth - 6, "%s", "...");
    }

    if (has_colors()) {
        wattron(overlay.get(), COLOR_PAIR(static_cast<int>(ColorToken::ModalText)) | A_BOLD);
    } else {
        wattron(overlay.get(), A_BOLD);
    }
    mvwhline(overlay.get(), overlayHeight - 2, 2, ' ', overlayWidth - 4);
    mvwaddnstr(overlay.get(), overlayHeight - 2, 2, "Enter/Esc dismiss   q quit", -1);
    if (has_colors()) {
        wattroff(overlay.get(), COLOR_PAIR(static_cast<int>(ColorToken::ModalText)) | A_BOLD);
    } else {
        wattroff(overlay.get(), A_BOLD);
    }

    wrefresh(overlay.get());
}

void ModalsView::drawLegalScreen(int rows, int cols, const std::string& title, const std::string& subtitle, const std::string& body) {
    erase();

    const int inset = 2;
    const int boxTop = inset;
    const int boxLeft = inset;
    const int boxHeight = std::max(8, rows - inset * 2);
    const int boxWidth = std::max(20, cols - inset * 2);
    const int bodyWidth = std::max(20, boxWidth - 6);
    const auto wrappedBody = wrapText(body, bodyWidth);
    const int maxBodyLines = std::max(1, boxHeight - 8);

    if (has_colors()) {
        attron(COLOR_PAIR(1) | A_BOLD);
    } else {
        attron(A_BOLD);
    }
    mvprintw(0, 2, "RetroWave");
    if (has_colors()) {
        attroff(COLOR_PAIR(1) | A_BOLD);
    } else {
        attroff(A_BOLD);
    }
    mvprintw(0, 14, "Legal Notice");

    Theme defaultTheme;
    defaultTheme.borderStyle = BorderStyle::Rounded;
    drawFrameLocal(boxTop, boxLeft, boxHeight, boxWidth, title.c_str(), defaultTheme);

    if (has_colors()) {
        attron(COLOR_PAIR(3) | A_BOLD);
    } else {
        attron(A_BOLD);
    }
    drawTextLine(boxTop + 2, boxLeft + 3, bodyWidth, subtitle);
    if (has_colors()) {
        attroff(COLOR_PAIR(3) | A_BOLD);
    } else {
        attroff(A_BOLD);
    }

    for (int index = 0; index < std::min<int>(wrappedBody.size(), maxBodyLines); ++index) {
        drawTextLine(boxTop + 4 + index, boxLeft + 3, bodyWidth, wrappedBody[static_cast<std::size_t>(index)]);
    }

    if (static_cast<int>(wrappedBody.size()) > maxBodyLines) {
        mvprintw(boxTop + 4 + maxBodyLines - 1, boxLeft + boxWidth - 6, "%s", "...");
    }

    if (has_colors()) {
        attron(COLOR_PAIR(6) | A_BOLD);
    } else {
        attron(A_BOLD);
    }
    mvprintw(boxTop + boxHeight - 3, boxLeft + 3, "%s", "Enter/Esc dismiss   q quit");
    if (has_colors()) {
        attroff(COLOR_PAIR(6) | A_BOLD);
    } else {
        attroff(A_BOLD);
    }
}

void ModalsView::drawSettingsOverlay(int rows, int cols, int selectedIndex, const AppSettings& settings, LayoutPreset layoutPreset, const Theme& theme) {
    const int overlayWidth = std::min(cols - 8, 64);
    const int overlayHeight = 17;
    const int top = std::max(2, (rows - overlayHeight) / 2);
    const int left = std::max(2, (cols - overlayWidth) / 2);

    ScopedWindow overlay(newwin(overlayHeight, overlayWidth, top, left));
    if (!overlay) {
        return;
    }

    if (has_colors()) {
        wbkgd(overlay.get(), COLOR_PAIR(static_cast<int>(ColorToken::ModalBg)) | ' ');
    } else {
        wbkgd(overlay.get(), A_REVERSE);
    }
    werase(overlay.get());
    if (has_colors()) wattron(overlay.get(), COLOR_PAIR(static_cast<int>(ColorToken::ModalBorder)));
    box(overlay.get(), 0, 0);
    if (has_colors()) wattroff(overlay.get(), COLOR_PAIR(static_cast<int>(ColorToken::ModalBorder)));

    wattron(overlay.get(), A_BOLD);
    if (has_colors()) wattron(overlay.get(), COLOR_PAIR(static_cast<int>(ColorToken::ModalTitle)));
    mvwprintw(overlay.get(), 0, 2, " Settings ");
    if (has_colors()) wattroff(overlay.get(), COLOR_PAIR(static_cast<int>(ColorToken::ModalTitle)));
    wattroff(overlay.get(), A_BOLD);

    if (has_colors()) wattron(overlay.get(), COLOR_PAIR(static_cast<int>(ColorToken::ModalText)));
    
    mvwaddnstr(overlay.get(), 2, 2, "Cover renderer", -1);
    int attr0 = selectedIndex == 0 ? A_REVERSE : A_BOLD;
    if (has_colors()) wattron(overlay.get(), attr0 | COLOR_PAIR(static_cast<int>(ColorToken::ModalText)));
    else wattron(overlay.get(), attr0);
    mvwprintw(overlay.get(), 3, 4, "< %s >", coverArtModeLabel(settings.coverArtMode));
    if (has_colors()) wattroff(overlay.get(), attr0 | COLOR_PAIR(static_cast<int>(ColorToken::ModalText)));
    else wattroff(overlay.get(), attr0);

    mvwaddnstr(overlay.get(), 5, 2, "Theme", -1);
    int attr1 = selectedIndex == 1 ? A_REVERSE : A_BOLD;
    if (has_colors()) wattron(overlay.get(), attr1 | COLOR_PAIR(static_cast<int>(ColorToken::ModalText)));
    else wattron(overlay.get(), attr1);
    mvwprintw(overlay.get(), 6, 4, "< %s >", theme.name.c_str());
    if (has_colors()) wattroff(overlay.get(), attr1 | COLOR_PAIR(static_cast<int>(ColorToken::ModalText)));
    else wattroff(overlay.get(), attr1);

    const char* layoutLabel = "";
    switch (layoutPreset) {
        case LayoutPreset::Default: layoutLabel = "Default"; break;
        case LayoutPreset::VisualizerFocus: layoutLabel = "Visualizer"; break;
        case LayoutPreset::LyricsFocus: layoutLabel = "Lyrics"; break;
        case LayoutPreset::Compact: layoutLabel = "Compact"; break;
    }

    mvwaddnstr(overlay.get(), 8, 2, "Layout", -1);
    int attr2 = selectedIndex == 2 ? A_REVERSE : A_BOLD;
    if (has_colors()) wattron(overlay.get(), attr2 | COLOR_PAIR(static_cast<int>(ColorToken::ModalText)));
    else wattron(overlay.get(), attr2);
    mvwprintw(overlay.get(), 9, 4, "< %s >", layoutLabel);
    if (has_colors()) wattroff(overlay.get(), attr2 | COLOR_PAIR(static_cast<int>(ColorToken::ModalText)));
    else wattroff(overlay.get(), attr2);

    mvwprintw(overlay.get(), 11, 2, "Volume is saved automatically: %3d%%", static_cast<int>(std::round(settings.volume * 100.0F)));
    mvwprintw(overlay.get(), 12, 2, "Repeat mode: %s", repeatModeLabel(settings.repeatMode));
    mvwprintw(overlay.get(), 13, 2, "Shuffle mode: %s", shuffleModeLabel(settings.shuffleMode));
    mvwaddnstr(overlay.get(), 14, 2, "Up/Down select   Left/Right change   r repeat   h shuffle", -1);
    mvwaddnstr(overlay.get(), 15, 2, "Enter/Esc close", -1);
    
    if (has_colors()) wattroff(overlay.get(), COLOR_PAIR(static_cast<int>(ColorToken::ModalText)));

    wnoutrefresh(overlay.get());
}

} // namespace retrowave
