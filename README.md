# RetroWave

RetroWave is a local terminal music player written in C++ for people who want a tactile, keyboard-first audio player instead of a desktop app.

It combines a classic `ncurses` TUI with:

- playlist navigation
- hotkeys for playback control
- ASCII album art rendered from embedded or nearby cover images
- animated visualizer
- `.lrc` lyric mode with active-line highlighting and auto-scroll
- a local `man` page

The project is intentionally built as a polished pet project for GitHub: compact, hackable, and visually opinionated rather than minimal boilerplate.

## Status

RetroWave is currently usable as a local player for a music folder on macOS.

Current scope:

- macOS audio output via `AudioToolbox`
- decoding through FFmpeg
- terminal UI via `ncurses`
- local lyrics lookup from sibling `.lrc` files
- album art extraction from embedded art or nearby image files

Current limitations:

- audio backend is macOS-only
- tracks are still decoded eagerly into memory
- no seek support yet
- no shuffle/repeat mode yet

## Features

- Browse and play local files or directories recursively
- Support for common formats via FFmpeg: `mp3`, `flac`, `wav`, `m4a`, `ogg`, `opus`, `aac`, `aiff`, `alac`, `wma`
- Album card with metadata and ASCII-converted cover art
- Toggle between `Visualizer` and `Lyrics` panels with `t`
- Timed lyric highlighting and auto-centering for `.lrc` files
- Real-time level meter and animated visualizer
- In-app modal error overlay instead of terminal log spam
- Project manual available through `man`

## Demo Flow

Run RetroWave against a music directory:

```bash
./build/retrowave ~/Music
```

If no path is provided, RetroWave scans the current working directory recursively for supported audio files.

## Requirements

### Runtime

- macOS

### Build dependencies

- CMake 3.20+
- C++20 compiler
- `pkg-config`
- FFmpeg with:
  - `libavformat`
  - `libavcodec`
  - `libavutil`
  - `libswresample`
  - `libswscale`
- `ncurses`

### Example install with Homebrew

```bash
brew install cmake pkg-config ffmpeg ncurses
```

## Build

### Standard build

```bash
cmake -S . -B build
cmake --build build
```

### Full rebuild

```bash
bash ./rebuild.sh
```

This removes the CMake build directory, regenerates it, and rebuilds RetroWave from scratch.

## Usage

```bash
./build/retrowave /path/to/music
```

You can pass:

- a single track
- multiple files
- one or more directories

## Manual Page

RetroWave ships with a local man page:

```bash
MANPATH="$PWD/man" man retrowave
```

## Hotkeys

- `Up` / `Down`: move playlist selection
- `Enter`: play selected track
- `Space`: pause or resume
- `n`: play next track
- `p`: play previous track
- `+` / `-`: adjust volume
- `t`: toggle visualizer and lyrics panel
- `w`: show warranty notice
- `c`: show redistribution notice
- `q`: quit

## Lyrics

RetroWave looks for lyrics in a sibling `.lrc` file with the same stem as the current track:

```text
song.mp3
song.lrc
```

If timed lyrics are present:

- the active line is highlighted
- past lines are dimmed
- the panel auto-scrolls to keep the current line in view

If no `.lrc` file is found, the lyrics panel shows a clear fallback message.

## Album Art

RetroWave tries these sources in order:

1. embedded attached picture in the audio file
2. nearby image files such as:
   - `cover.jpg`
   - `cover.png`
   - `folder.jpg`
   - `front.jpg`
   - `album.jpg`

The selected image is converted to grayscale and rendered as ASCII inside the album card.

## Project Layout

```text
src/
  audio/      decoding, playback, output
  core/       playlist and lyrics loading
  ui/         ncurses interface
man/
  man1/       local manual page
```

Key files:

- `src/main.cpp` - program entry point and FFmpeg log suppression
- `src/audio/AudioDecoder.cpp` - decoding, metadata, album art extraction
- `src/audio/PlaybackEngine.cpp` - playback state and visualizer/lyrics timing
- `src/ui/TerminalUI.cpp` - terminal layout and interaction
- `src/core/Lyrics.cpp` - `.lrc` discovery and parsing

## Performance Notes

Recent optimization work includes:

- switching decoded PCM storage to `int16_t`
- updating visualizer bins from the real audio output buffer
- compensating lyric timing for queued audio latency
- reducing unnecessary TUI redraw pressure

There is still room to improve memory usage by moving from eager decode to streaming decode.

## Roadmap

- streaming decode to reduce RAM usage on long tracks
- seeking
- shuffle / repeat
- better album-art downsampling for tiny embedded covers
- richer metadata display
- optional Linux audio backend

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

The repository currently includes a project notice in [NOTICE](NOTICE):

```text
RetroWave  Copyright (C) 2026 Viktor Voloshko
This program comes with ABSOLUTELY NO WARRANTY; for details press w.
This is free software, and you are welcome to redistribute it
under certain conditions; press c for details.
```

That notice is visible in the TUI through the `w` and `c` hotkeys.

The full license text is included in [LICENSE.txt](LICENSE.txt). RetroWave is currently distributed under GPLv3.
