#!/usr/bin/env python3
import argparse
import os
import pty
import select
import signal
import subprocess
import sys
import time


def main() -> int:
    parser = argparse.ArgumentParser(description="Drive RetroWave TUI through a pseudo-terminal.")
    parser.add_argument("--timeout", type=float, default=12.0)
    parser.add_argument("--delay", type=float, default=1.0)
    parser.add_argument("--keys", default="q")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()

    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        parser.error("missing command")

    master_fd, slave_fd = pty.openpty()
    env = os.environ.copy()
    env.setdefault("TERM", "xterm-256color")

    process = subprocess.Popen(
        command,
        stdin=slave_fd,
        stdout=slave_fd,
        stderr=slave_fd,
        env=env,
        start_new_session=True,
    )
    os.close(slave_fd)

    deadline = time.monotonic() + args.timeout
    next_key_at = time.monotonic() + args.delay
    key_index = 0
    output = bytearray()

    try:
        while time.monotonic() < deadline:
            if process.poll() is not None:
                return process.returncode

            readable, _, _ = select.select([master_fd], [], [], 0.05)
            if readable:
                try:
                    chunk = os.read(master_fd, 4096)
                except OSError:
                    chunk = b""
                if chunk:
                    output.extend(chunk)

            if key_index < len(args.keys) and time.monotonic() >= next_key_at:
                os.write(master_fd, args.keys[key_index].encode("utf-8"))
                key_index += 1
                next_key_at = time.monotonic() + args.delay

        os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=1.0)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait(timeout=2.0)

        sys.stderr.write("RetroWave TUI smoke timed out.\n")
        if output:
            sys.stderr.buffer.write(output[-4096:])
        return 124
    finally:
        os.close(master_fd)


if __name__ == "__main__":
    raise SystemExit(main())
