# Contributing to RetroWave

Thanks for contributing.

RetroWave is a small C++ terminal player project, so the goal is to keep the codebase understandable, practical, and easy to iterate on. Contributions are welcome, but they should preserve the project's tone: local-first, keyboard-first, and compact.

## Before You Start

Please open an issue or start a discussion before working on larger changes such as:

- new audio backend support
- major UI redesigns
- streaming/decode architecture changes
- metadata/database features

For focused bug fixes and small quality-of-life improvements, a pull request is fine directly.

## Development Setup

### Dependencies

- macOS
- CMake 3.20+
- C++20 compiler
- `pkg-config`
- FFmpeg
- `ncurses`

Example with Homebrew:

```bash
brew install cmake pkg-config ffmpeg ncurses
```

### Build

```bash
cmake -S . -B build
cmake --build build
```

Or do a clean rebuild:

```bash
bash ./rebuild.sh
```

### Run

```bash
./build/retrowave /path/to/music
```

### Manual Page

```bash
MANPATH="$PWD/man" man retrowave
```

## Project Priorities

When making changes, optimize for:

- clear terminal UX
- predictable keyboard interaction
- low accidental complexity
- practical local-media workflows
- maintainable code over clever abstractions

## Coding Guidelines

- Use C++20.
- Keep files focused and avoid over-abstracting small flows.
- Prefer straightforward code over framework-like architecture.
- Preserve the current naming style and formatting in nearby files.
- Keep comments sparse and useful.
- Avoid adding dependencies unless they materially improve the project.

## UI Guidelines

- Terminal layout should stay readable on realistic terminal sizes.
- Do not reintroduce persistent noisy help bars if the same information fits better in docs or `man`.
- Error handling should stay inside the TUI rather than printing over `ncurses`.
- Visual additions should not materially increase CPU usage while idle.

## Performance Expectations

Performance matters for this project.

Please be careful with:

- repeated large allocations in the UI path
- scanning or transforming full-track buffers every frame
- excessive redraw frequency
- unnecessary metadata or image work during playback

If a change risks higher memory or CPU usage, call that out explicitly in the pull request.

## Testing

There is no full automated test suite yet.

At minimum, validate:

1. the project builds cleanly
2. the player launches
3. playlist navigation still works
4. playback still starts
5. lyrics mode still behaves correctly when `.lrc` is present and absent

Suggested commands:

```bash
bash ./rebuild.sh
./build/retrowave /path/to/music
```

## Pull Requests

Good pull requests are:

- scoped
- clearly explained
- easy to review

Include:

- what changed
- why it changed
- how you validated it
- any known limitations or follow-up work

## Commit Messages

Prefer short, direct commit messages such as:

- `add local man page`
- `improve album art rendering`
- `reduce playback memory usage`

## What Not to Change Casually

Please avoid drive-by changes to these areas without clear justification:

- the overall TUI information hierarchy
- the playback engine threading model
- the current local-first file discovery behavior
- the docs tone and project positioning

## Questions

If something is unclear, open an issue or annotate the pull request with the tradeoff you are unsure about.
