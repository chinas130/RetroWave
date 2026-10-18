# Audio Pipeline Architecture

The audio pipeline in RetroWave is responsible for decoding, buffering, and rendering audio streams efficiently, with support for a wide variety of formats, local files, and network streams. This document outlines the architecture and implementation details of the audio subsystem.

## 1. Decoding and Playback Architecture

### PlaybackEngine
The `PlaybackEngine` serves as the central control module of the audio subsystem. It employs a multi-threaded architecture to ensure seamless playback without blocking the main application thread. The engine runs two primary background loops:

*   **`loaderLoop` (Background Loader Thread):** Responsible for asynchronously pre-fetching metadata, resolving network streams (such as YouTube or HLS URLs), and caching album art or lyrics for the upcoming track.
*   **`decoderLoop` (Decoder Thread):** Continuously decodes audio frames using FFmpeg and pushes them into the audio ring buffer, ensuring the buffer remains adequately filled.
*   **`AudioOutput`:** A separate hardware interface component that pulls raw PCM frames from the ring buffer and pushes them to the audio backend (e.g., CoreAudio, ALSA, or WASAPI) for actual rendering.

### AudioRingBuffer
The `AudioRingBuffer` acts as a lock-free synchronization bridge between the `decoderLoop` and the `AudioOutput`. It is designed for ultra-low latency and guarantees zero blocking on the audio rendering thread.

*   **Power of Two Alignment:** The buffer capacity is automatically aligned to the nearest power of two during initialization using `std::bit_ceil`. This allows the buffer to use fast modulo arithmetic via bitwise AND operations (`& mask`) instead of expensive integer division when wrapping read/write indices.
*   **Lock-Free Atomics:** Reading and writing positions are managed via lock-free atomic indices (`std::atomic<std::size_t>`). Threads synchronize using acquire/release memory semantics (`std::memory_order_acquire` when reading indices and `std::memory_order_release` when updating them). This avoids the overhead of mutex locks.
*   **Zero-Blocking:** Because it is lock-free, the audio rendering thread never blocks waiting for the decoder, ensuring glitch-free audio playback even during heavy CPU load.

```cpp
// Example of lock-free index fetching and updating in AudioRingBuffer
const std::size_t currentWrite = writePos_.load(std::memory_order_acquire);
const std::size_t currentRead = readPos_.load(std::memory_order_relaxed);

// ... push operations ...

writePos_.store(currentWrite + framesToWrite, std::memory_order_release);
```

## 2. Supported Formats and FFmpeg Integration

RetroWave leverages `libavformat` and `libavcodec` from the FFmpeg ecosystem to natively decode a vast array of audio formats without relying on external system codecs.

*   **Supported Formats:** `mp3`, `flac`, `wav`, `m4a`, `ogg`, `opus`, `aac`, `aiff`, `alac`, `wma`

### Resampling and Mixing
All decoded audio is normalized to a standard internal format using `libswresample` (`SwrContext`) before being placed in the `AudioRingBuffer`. The standardized output is:

*   **Sample Rate:** 44.1 kHz
*   **Channels:** Stereo (2 channels)
*   **Format:** PCM 16-bit little-endian (`S16_LE`)

**Preventing Clipping on Downmix:**
When downmixing multi-channel audio (e.g., 5.1 or 7.1 surround sound to stereo), the `rematrix_volume` parameter is explicitly applied within the `SwrContext`. This prevents clipping and audio distortion by properly scaling the amplitude of the combined channels.

```cpp
// Applying matrix mixdown parameters in AudioStreamDecoder
av_opt_set_double(resampler_, "rematrix_volume", 1.0, 0);
```

## 3. Playlists and Network Streams

### Local Files and Playlists
The core `Playlist` module supports recursively traversing directories and scanning for local audio files. It natively parses standard playlist formats:

*   **Formats:** `.m3u`, `.m3u8`, `.pls`
*   **Features:** It handles relative path resolution based on the playlist file's location, parses `#EXTINF` tags for track metadata (titles and durations), and supports the standard `[playlist]` section format structure of `.pls` files.

### Network Streams and Radio
Direct network streams and internet radio URLs are handled transparently by the engine.

*   Streams are opened using FFmpeg's `avio_open2` via the `avformat_open_input` interface.
*   The decoder seamlessly handles indefinite live broadcasts (`LIVE` streams) where the duration is unknown or infinite, ensuring continuous playback and dynamic metadata extraction when available.

## 4. HLS and YouTube Integration

### HLS Manifests
HTTP Live Streaming (HLS) manifests (typically `.m3u8` files containing `#EXT-X-*` tags) require special handling due to their segmented nature. To ensure robust playback, RetroWave pipes these streams through an external `ffmpeg` process. This spawned process acts as a robust network bridge, outputting a continuous PCM pipe (`pipe:1`) that the `AudioStreamDecoder` can consume natively without dealing with segment transitions internally.

### YouTube Integration
YouTube links are processed seamlessly via the `YtDlpResolver` class.

*   When a YouTube URL is detected (via `YtDlpResolver::isYouTubeUrl`), `YtDlpResolver::resolve` invokes the external `yt-dlp` utility to extract the direct, underlying audio stream URL.
*   This direct URL is then passed to the FFmpeg backend, allowing RetroWave to stream YouTube audio directly without needing to download the entire video or rely on complex web views.
