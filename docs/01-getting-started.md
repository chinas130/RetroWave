# Getting Started with RetroWave

## Introduction

RetroWave is a local, keyboard-oriented terminal UI (TUI) music player written in C++20. It is built natively without heavy web frameworks, delivering a tactile and highly responsive audio playback experience.

The core philosophy embraces a local-first approach. It targets power users, geeks, and tiling window manager enthusiasts who prefer an efficient, terminal-based workflow over monolithic desktop applications.

## System Requirements

To build and run RetroWave, your system must meet the following requirements:

- **Build System:** CMake 3.20+
- **Compiler:** C++20 compatible compiler (e.g., GCC, Clang, Apple Clang)
- **Tooling:** `pkg-config`
- **Libraries:**
  - **FFmpeg:** `libavformat`, `libavcodec`, `libavutil`, `libswresample`, `libswscale`
  - **ncurses:** `ncursesw` (wide-character support)
- **CLI Utilities:**
  - `ffmpeg` executable (for on-the-fly stream conversion)
  - `yt-dlp` (optional, for resolving YouTube URLs)
- **System Audio Backends:**
  - **macOS:** `AudioToolbox` (native system framework)
  - **Linux:** `ALSA`

## Installing Dependencies

### macOS (Homebrew)

```bash
brew install cmake pkg-config ffmpeg ncurses yt-dlp
```

### Debian / Ubuntu (APT)

```bash
sudo apt update
sudo apt install cmake pkg-config ffmpeg yt-dlp \
    libavformat-dev libavcodec-dev libavutil-dev libswresample-dev libswscale-dev \
    libncursesw5-dev libasound2-dev
```

## Build and Run

### 1. Clone the Repository

```bash
git clone https://github.com/username/retrowave.git
cd retrowave
```
*(Adjust the repository URL based on your origin).*

### 2. Standard Build

Generate the build system and compile the binary:

```bash
cmake -S . -B build
cmake --build build
```

### 3. Fast Rebuild

For a clean, scratch rebuild during development, use the provided script:

```bash
bash ./rebuild.sh
```

### 4. Starting a Session

Run the compiled executable and point it to a music directory:

```bash
./build/retrowave ~/Music
```

*(If no path is provided, the player recursively scans the current working directory for supported audio formats).*

## Project Structure

A quick overview of the key directories in the repository:

- `src/audio/`: Decoding, metadata extraction, playback engine, and audio output backends.
- `src/core/`: Internal logic, playlist management, `.lrc` lyrics parsing, and settings.
- `src/ui/`: The `ncurses` terminal layout, theme rendering, and keyboard binding.
- `tests/`: Automated test suite for core components (ring buffer, parsers, playback edge cases).
- `man/`: The local manual page (`retrowave.1`).
