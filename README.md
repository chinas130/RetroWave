# RetroWave

RetroWave is a local terminal music player written in C++ for people who want a tactile, keyboard-first audio player instead of a desktop app.

It combines a classic `ncurses` TUI with:

- playlist navigation
- hotkeys for playback control
- album art rendered from embedded or nearby cover images
- animated visualizer
- `.lrc` lyric mode with active-line highlighting and auto-scroll
- a local `man` page

The project is intentionally built as a polished pet project for GitHub: compact, hackable, and visually opinionated rather than minimal boilerplate.

## Status

RetroWave is currently usable as a local player for a music folder on macOS and Linux.

Current scope:

- macOS audio output via `AudioToolbox`
- Linux audio output via ALSA
- decoding through FFmpeg
- terminal UI via `ncurses`
- local lyrics lookup from sibling `.lrc` files
- album art extraction from embedded art or nearby image files

Current limitations:

- no official Homebrew bottle or tap publication yet
- no seek support yet
- no shuffle/repeat mode yet

## Features

- Browse and play local files or directories recursively
- Support for common formats via FFmpeg: `mp3`, `flac`, `wav`, `m4a`, `ogg`, `opus`, `aac`, `aiff`, `alac`, `wma`
- Album card with metadata and multiple text-mode cover renderers: `ASCII`, `Block Shading`, `Half-Block`
- Toggle between `Visualizer` and `Lyrics` panels with `t`
- Timed lyric highlighting and auto-centering for `.lrc` files
- Settings modal with persisted volume and cover-art mode
- Animated visualizer
- In-app modal error overlay instead of terminal log spam
- Project manual available through `man`

## Demo Flow

Run RetroWave against a music directory:

```bash
./build/retrowave ~/Music
```

If no path is provided, RetroWave scans the current working directory recursively for supported audio files.

## Requirements

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
- ALSA development headers on Linux

### Example install with Homebrew

```bash
brew install cmake pkg-config ffmpeg ncurses
```

On Debian/Ubuntu:

```bash
sudo apt install cmake pkg-config libavformat-dev libavcodec-dev libavutil-dev libswresample-dev libswscale-dev libncursesw5-dev libasound2-dev
```

## Build

### Standard build

```bash
cmake -S . -B build
cmake --build build
```

### Install to a prefix

```bash
cmake --install build --prefix /tmp/retrowave-stage
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
- `s`: open settings modal
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

The selected image is converted to grayscale and rendered inside the album card using the currently selected text-mode renderer.

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
- `src/core/Settings.cpp` - persisted app settings

## Performance Notes

Recent optimization work includes:

- moving playback to streaming decode with a ring buffer
- lazy waveform generation in the background
- updating visualizer bins from the real audio output buffer
- compensating lyric timing for queued audio latency
- reducing unnecessary TUI redraw pressure

## Roadmap

- seeking
- shuffle / repeat
- better album-art downsampling for tiny embedded covers
- richer metadata display
- disk-backed waveform cache

## CI/CD

GitHub Actions now covers two paths:

- `.github/workflows/ci.yml`
  - builds on `ubuntu-latest` and `macos-latest`
  - verifies `cmake --install`
  - runs a small CLI smoke test
- `.github/workflows/release.yml`
  - triggers on tags like `v0.1.0`
  - builds binary packages on `ubuntu-latest` and `macos-latest`
  - creates a source tarball release asset
  - emits a matching `sha256` file
  - renders a ready-to-publish `retrowave.rb` Homebrew formula

## GitHub Releases

GitHub Releases are always attached to git tags, but users should download builds from the release page rather than the tag page.

To publish a release:

```bash
git tag v0.1.0
git push origin v0.1.0
```

The `Release` workflow creates a GitHub Release for that tag and uploads assets such as:

- `retrowave-0.1.0-linux-x86_64.tar.gz`
- `retrowave-0.1.0-macos-arm64.tar.gz` or another macOS runner architecture
- `retrowave-0.1.0.tar.gz`
- `retrowave-0.1.0.sha256`
- `retrowave.rb`

The binary packages contain the installed `bin`, `share/man`, and `share/doc` tree. They are convenience builds, not fully static bundles, so FFmpeg/ncurses/audio backend runtime libraries still need to be available on the target system.

## Personal Homebrew Tap Flow

The release workflow can update a personal Homebrew tap. This is separate from publishing to the official `Homebrew/homebrew-core` repository.

1. Create a tap repository such as `yourname/homebrew-retrowave`.
2. Add a repository variable in the main repo:
   - `HOMEBREW_TAP_REPOSITORY=yourname/homebrew-retrowave`
3. Add a repository secret with write access to that tap:
   - `HOMEBREW_TAP_TOKEN`
4. Push a release tag as shown in the GitHub Releases section.

That tag will publish:

- `retrowave-0.1.0.tar.gz`
- `retrowave-0.1.0.sha256`
- `retrowave.rb`

It will also publish the macOS/Linux binary archives on the GitHub Release page.

If the tap repository variable and token are configured, the workflow also updates `Formula/retrowave.rb` in the tap automatically.

Once the tap exists, installation looks like:

```bash
brew tap yourname/retrowave
brew install retrowave
```

Publishing to `Homebrew/homebrew-core` is a separate future step and requires opening a PR against Homebrew's official formula repository.

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
