# 5. Configuration, Theming, and IPC

This document describes the configuration system, theme engine, inter-process communication (IPC) daemon, and event hooks for RetroWave.

## 1. Configuration File (`config.toml`)

RetroWave adheres to the XDG Base Directory Specification. The main configuration file is located at `~/.config/retrowave/config.toml`.

### General Section
The `[general]` section stores application-wide settings and state. To prevent data corruption, settings are saved atomically by writing to a temporary file and performing an atomic rename.

```toml
[general]
volume = 85
repeat_mode = "all"        # "off", "track", "all"
shuffle_mode = false
theme = "catppuccin-mocha" # active theme
layout_preset = "default"  # UI layout preset
cover_art_mode = "ascii"   # "off", "ascii", "braille", "sixel", "kitty"
```

## 2. Key Bindings (`KeyBinder`)

RetroWave allows fully customizable hotkeys via the `[keys]` section in `config.toml`.

### Configuration
The `KeyBinder` component supports various formats, including string aliases (`space`, `enter`, `esc`, `tab`, `up`, `down`, `left`, `right`, `page_up`, `page_down`) and combinations with modifiers (e.g., `ctrl+c`). Capital letters are automatically mapped for typing convenience.

```toml
[keys]
play_pause = "space"
stop = "s"
next_track = "n"
prev_track = "p"
volume_up = "up"
volume_down = "down"
seek_forward = "right"
seek_backward = "left"
quit = "ctrl+c"
toggle_playlist = "tab"
```

## 3. Theme Engine (`ThemeManager`)

The UI relies on a semantic token-based theme engine (`ThemeManager`) rather than hardcoded colors, mapping elements to a unified `ColorToken` enumeration.

### Semantic Tokens
Tokens control the coloring of frames, headers, playlist items, the timeline, visualizer, and lyrics. 

### Color Formats and Fallbacks
The theme engine parses the following color formats in TOML:
* `"default"`: Uses the terminal's default transparent or system color.
* Hex strings: `"#RRGGBB"` for TrueColor support.
* Numeric indices: `0` to `255` for standard 256-color palettes.

The engine provides smart fallbacks for terminals lacking TrueColor support, approximating the nearest 256-color equivalent.

### Presets and Custom Themes
RetroWave ships with built-in presets: `retrowave`, `catppuccin-mocha`, `gruvbox-dark`, `nord`, and `monochrome`. 
Custom themes can be loaded dynamically by placing TOML files in `~/.config/retrowave/themes/`.

**Example: `~/.config/retrowave/themes/custom-synth.toml`**
```toml
[theme]
name = "Custom Synth"
border_style = "Rounded" # Single, Rounded, Double, Ascii
viz_style = "Braille"    # Blocks, Braille, Ascii

[colors]
frame = "#FF00FF"
header = "#00FFFF"
playlist_active = "#FFFF00"
timeline = "#FF00AA"
visualizer = "#00FFCC"
lyrics = "default"
```

## 4. IPC Daemon and CLI Control (`IpcServer`)

RetroWave supports a headless/daemon mode and remote control capabilities, enabling external scripts and window managers to interact with the player.

### UNIX Domain Sockets
Communication is handled via a UNIX Domain Socket created at `/tmp/retrowave-$UID.sock`. 

### Remote CLI Mode
When RetroWave is already running, invoking CLI commands (e.g., `retrowave next`, `retrowave status`, `retrowave volume +10`) will not spawn a new TUI instance. Instead, the CLI routes the command to the active IPC daemon and outputs the result to `stdout`.

* For actions like `next` or `volume +10`, it sends the command silently or with brief confirmation.
* For querying state (`retrowave status`), it returns correctly escaped JSON, ideal for integration with tools like Polybar, Waybar, or tmux.

**Example CLI usage:**
```bash
$ retrowave status
{"state": "playing", "artist": "Kavinsky", "title": "Nightcall", "volume": 85}

$ retrowave volume +10
Volume set to 95
```

### IPC Security and Stability
The `IpcServer` parser is resilient against malformed input. Exceptions like `std::invalid_argument` encountered during argument parsing are gracefully caught and logged, preventing rogue commands from crashing the player.

## 5. Scripting Event Hooks (`EventHooks`)

For deeper system integration (such as updating Discord Rich Presence or sending desktop notifications), the `EventHooks` module triggers user-defined shell scripts on specific playback events.

Hooks are placed in the `~/.config/retrowave/hooks/` directory. They are launched asynchronously, ensuring that neither the audio thread nor the ncurses interface is blocked.

### Available Hooks

* **`on-track-change.sh`**
  Triggered when a new track starts.
  **Exported Variables:** `$TITLE`, `$ARTIST`, `$ALBUM`, `$DURATION`, `$PATH`

  *Example (`on-track-change.sh`):*
  ```bash
  #!/usr/bin/env bash
  notify-send "Now Playing" "$TITLE by $ARTIST\nAlbum: $ALBUM" -i /usr/share/icons/music.png
  ```

* **`on-state-change.sh`**
  Triggered when the playback state changes.
  **Exported Variables:** `$STATE` (can be `playing`, `paused`, or `stopped`)

  *Example (`on-state-change.sh`):*
  ```bash
  #!/usr/bin/env bash
  if [ "$STATE" = "paused" ]; then
      echo "Playback paused" > /tmp/retrowave_status
  fi
  ```
