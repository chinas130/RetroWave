# RetroWave TUI Layouts & Theming

This document details the terminal user interface (TUI) architecture, responsive layout rendering, and visualizer components in RetroWave. The interface is built on top of `ncursesw` for wide-character (Unicode) support and provides highly adaptive pane management.

## 1. TUI Architecture & Responsiveness

### Initialization
RetroWave utilizes `ncursesw` to ensure full compatibility with wide characters, sub-pixel block elements, and Braille patterns. During application startup, `setlocale(LC_ALL, "")` is invoked (typically in the main entry point) to enable Unicode mode.

The display is initialized within `TerminalUI::run()` using standard `ncurses` directives:

```cpp
initscr();
cbreak();
noecho();
keypad(stdscr, TRUE);
curs_set(0);
if (has_colors()) {
    start_color();
    use_default_colors();
    themeManager_.applyToNcurses();
}
```

By leveraging `use_default_colors()`, the application respects the user's default terminal background transparency and colors, integrating seamlessly into the host environment.

### Adaptive Geometry
Instead of enforcing rigid dimensional constraints, the minimum terminal size threshold has been optimized down to a comfortable **`48x10`**. If the user's terminal size drops below these dimensions, `TerminalUI::draw()` intercepts the render loop and automatically displays a warning message instructing the user to resize the window:

```cpp
const int minWidth = 48;
const int minHeight = 10;
if (rows < minHeight || cols < minWidth) {
    erase();
    mvprintw(1, 2, "RetroWave needs at least %dx%d terminal size.", minWidth, minHeight);
    mvprintw(3, 2, "Current size: %dx%d", cols, rows);
    mvprintw(5, 2, "Resize the terminal to continue.");
    // ...
}
```

The pane geometry is dynamically recalculated each frame using `TerminalUI::computeLayout`. This method assesses the available rows and columns and selectively hides or resizes UI panels to avoid overflow:
- **Playlist Panel:** Hides entirely if the width drops below `60` columns (`cols < 60`).
- **Cover Art:** The ASCII album art is omitted if the width is less than `85` columns (`cols < 85`).

## 2. Layout Presets

RetroWave supports 4 distinct view presets, defined by the `LayoutPreset` enum, allowing users to tailor the interface for productivity, immersion, or karaoke.

```cpp
enum class LayoutPreset {
    Default,
    VisualizerFocus,
    LyricsFocus,
    Compact
};
```

- **`Default`**: The classic three-pane view. It displays the playlist on the left, and divides the right side into a track information card (with ASCII cover art, metadata, and a timeline) and a detail panel at the bottom for either the visualizer or lyrics.
- **`VisualizerFocus`**: An immersive spectrum analyzer mode. The playlist and cover art are hidden, and the visualizer is expanded to occupy roughly 75-80% of the terminal screen, leaving only a compact timeline and metadata header at the top.
- **`LyricsFocus`**: A dedicated karaoke mode. Metadata and playback controls are minimized, and `.lrc` lyrics lines are centered dynamically on the screen with smooth auto-scrolling. 
- **`Compact`**: A minimalist mini-player layout occupying only 4-6 terminal rows. It displays only the track title, time progress bar, and basic status, making it ideal for narrow windows or dropdown terminals.

Users can toggle these layout presets on the fly using the `Tab` key (mapped to the `Action::CycleLayoutPreset` action) or via the in-app settings menu.

## 3. Animated Visualizer

The audio visualizer transforms raw PCM audio signals into an array of spectral bins (e.g., `kPlaybackVisualizerBins = 64`). The rendering pipeline smooths these bins over time and applies an auto-gain algorithm to ensure the visualizer remains dynamic and visible regardless of the source track's volume mastering.

### Rendering Styles
The `VizStyle` enum dictates how the spectrum bars are drawn to the screen:

```cpp
enum class VizStyle {
    Blocks,
    Ascii,
    Braille
};
```

- **`Blocks`**: Uses sub-pixel Unicode block elements (` ▂▃▄▅▆▇█`) for smooth, high-resolution vertical bar levels.
- **`Braille`**: Leverages Unicode Braille dot patterns (`⡀`, `⣀`, `⣿`, etc.) to represent audio energy, offering a modern, dotted aesthetic.
- **`Ascii`**: A fallback mode utilizing standard characters (`#`, `|`, `.`) for maximum compatibility with legacy terminal emulators or environments missing wide-font support.

### Gradient Color Mapping
As bars are drawn in `TerminalUI::drawVisualizer`, their vertical height maps to distinct color tokens defined in `ColorToken`. The styling provides a gradient transition from the bottom of the spectrum to the top:

- **`ColorToken::VizBarLow`**: Applied to the bottom third of the bar.
- **`ColorToken::VizBarMid`**: Applied to the middle section.
- **`ColorToken::VizBarHigh`**: Applied to the top section.
- **`ColorToken::VizPeak`**: A distinctive highlight color applied exclusively to the topmost active cell of the bar (the peak cap) to visually emphasize the waveform's crest.

```cpp
int colorToken = static_cast<int>(ColorToken::VizBarLow);
if (row >= barHeight * 2 / 3) colorToken = static_cast<int>(ColorToken::VizBarHigh);
else if (row >= barHeight / 3) colorToken = static_cast<int>(ColorToken::VizBarMid);

if (row == filled - 1) colorToken = static_cast<int>(ColorToken::VizPeak);
```
