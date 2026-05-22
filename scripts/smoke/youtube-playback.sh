#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
binary="${RETROWAVE_BIN:-${repo_root}/build/retrowave}"
url="${RETROWAVE_YOUTUBE_URL:-}"
timeout_seconds="${RETROWAVE_SMOKE_TIMEOUT:-30}"
key_delay="${RETROWAVE_SMOKE_KEY_DELAY:-4}"

if [[ -z "${url}" ]]; then
  echo "Skipping YouTube smoke: set RETROWAVE_YOUTUBE_URL to enable it." >&2
  exit 0
fi

if [[ ! -x "${binary}" ]]; then
  echo "RetroWave binary is missing or not executable: ${binary}" >&2
  exit 1
fi

if ! command -v yt-dlp >/dev/null 2>&1; then
  echo "Skipping YouTube smoke: yt-dlp is not installed." >&2
  exit 0
fi

if ! command -v ffmpeg >/dev/null 2>&1; then
  echo "Skipping YouTube smoke: ffmpeg is not installed." >&2
  exit 0
fi

tmp_dir="$(mktemp -d)"
cleanup() {
  rm -rf "${tmp_dir}"
}
trap cleanup EXIT

cat > "${tmp_dir}/youtube.m3u" <<EOF
#EXTM3U
#EXTINF:-1,YouTube Smoke A
${url}
#EXTINF:-1,YouTube Smoke B
${url}
EOF

python3 "${repo_root}/scripts/smoke/tui-driver.py" \
  --timeout "${timeout_seconds}" \
  --delay "${key_delay}" \
  --keys "nq" \
  -- "${binary}" "${tmp_dir}/youtube.m3u"

if command -v pgrep >/dev/null 2>&1; then
  if pgrep -f "ffmpeg.*RetroWave/0.1" >/dev/null 2>&1; then
    echo "ffmpeg process survived after RetroWave exit." >&2
    exit 1
  fi
fi
