#!/usr/bin/env python3
"""Write a short mono WAV tone for local TUI smoke tests."""

from __future__ import annotations

import argparse
import math
import struct
import wave


def write_tone(
    path: str,
    *,
    sample_rate: int = 44100,
    duration_seconds: float = 1.0,
    frequency_hz: float = 440.0,
    amplitude: float = 0.25,
) -> None:
    frame_count = int(sample_rate * duration_seconds)
    with wave.open(path, "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)

        frames = bytearray()
        for index in range(frame_count):
            sample = amplitude * math.sin(2.0 * math.pi * frequency_hz * index / sample_rate)
            frames.extend(struct.pack("<h", int(sample * 32767.0)))

        wav_file.writeframes(frames)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "output",
        nargs="?",
        default="assets-test-tone.wav",
        help="Output WAV path (default: assets-test-tone.wav)",
    )
    args = parser.parse_args()
    write_tone(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
