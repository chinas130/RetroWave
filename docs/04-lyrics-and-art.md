# RetroWave Technical Documentation

## Part 4: Lyrics and Album Art Rendering

This document describes the internal mechanisms for loading, parsing, and rendering synchronized lyrics and album art in RetroWave. The implementation relies on core data structures defined in [`src/core/Lyrics.h`](../src/core/Lyrics.h) and [`src/audio/AudioDecoder.h`](../src/audio/AudioDecoder.h).

---

### 1. Synchronized Lyrics (`.lrc` & Enhanced LRC)

RetroWave manages lyric synchronization through the [`LyricsData`](../src/core/Lyrics.h#L24-L32) and [`LyricLine`](../src/core/Lyrics.h#L15-L22) structures. 

#### 1.1 Discovery and Fallbacks

The application uses a multi-tiered approach to locate lyrics for a playing track:
1. **Sibling `.lrc` Match**: Checks for an `.lrc` file with the exact same stem as the audio file.
2. **Recursive Directory Search**: Scans the parent directory to find any `.lrc` files matching the audio file's stem.
3. **Text Fallback**: Falls back to checking for identically named `.txt` files.
4. **Embedded Metadata**: If no external files are found, the decoder extracts embedded metadata tags using FFmpeg. It explicitly checks for standard keys such as `lyrics`, `syncedlyrics`, `unsyncedlyrics`, `USLT`, and `SYLT`.

#### 1.2 LRC Parser Features

The parser handles standard LRC conventions, including:
- **Time Offsets**: Parses the `[offset:+/-milliseconds]` tag to automatically shift all parsed timestamps.
- **Track Metadata**: Extracts standard header tags such as `[ar:]`, `[ti:]`, `[al:]`, `[by:]`, and `[length:]`.
- **Flexible Timecodes**: Supports multiple timestamp resolutions, accurately parsing:
  - `[mm:ss.xx]`
  - `[mm:ss.xxx]`
  - `[hh:mm:ss.xxx]`
- **Multiple Timecodes**: Fully supports multiple timecodes on a single line (e.g., `[01:10.00][02:20.00] Chorus text`), generating a distinct [`LyricLine`](../src/core/Lyrics.h#L15-L22) for each occurrence.

#### 1.3 Enhanced LRC (Word-Sync)

For sub-line synchronization, the parser implements Enhanced LRC capabilities:
- Parses granular, word-level inline tags formatted as `<mm:ss.xx>`.
- Divides lines into discrete [`LyricSegment`](../src/core/Lyrics.h#L10-L13) entities.
- The UI leverages these segments to provide dynamic text highlighting, visually distinguishing between past, active, and future words on the current active line.

#### 1.4 Memory and Overflow Protection

To ensure stability against malformed or malicious lyric files, strict limits are enforced:
- **Maximum File Size**: Files are truncated at `512 KB` (512 * 1024 bytes).
- **Maximum Line Count**: The parser halts processing after reaching `kMaxLyricLines = 3000`.

---

### 2. Album Art Rendering (`AlbumArt`)

Visual representations are extracted and decoded into a raw grayscale buffer stored within the [`AlbumArt`](../src/audio/AudioDecoder.h#L15-L23) structure.

#### 2.1 Extraction Priority

Cover art is identified using a prioritized fallback mechanism:
1. **Embedded Video Stream**: Extracts the built-in FFmpeg `attached_pic` stream directly from the audio file container.
2. **Local Stub Files**: Searches the track's directory for common placeholder image filenames: `cover.jpg`, `cover.png`, `folder.jpg`, `front.jpg`, and `album.jpg`.

#### 2.2 Grayscale Processing

Images are downscaled and converted to a raw grayscale byte array (`std::vector<std::uint8_t> grayscale`). 
This conversion is performed using `libswscale` with the high-quality `SWS_LANCZOS` filter to preserve visual details when compressing high-resolution covers into low-resolution terminal viewports.

#### 2.3 Terminal Rendering Modes (`CoverArtMode`)

The UI visualizes the grayscale buffer using one of three distinct text-based rendering modes (`CoverArtMode`):
- **`ASCII`**: Maps pixel luminance to a predefined 22-character ASCII scale:
  ```text
    ..,,::--==++**##%%@@
  ```
- **`BlockShading`**: Uses dense Unicode block shading characters (e.g., ░, ▒, ▓, █) for smoother gradients.
- **`Half-Block`**: Employs pseudo-graphic half-block characters (▄, ▀) to double the vertical pixel density, offering the highest fidelity rendering for terminal environments.
