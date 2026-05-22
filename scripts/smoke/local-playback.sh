#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
binary="${RETROWAVE_BIN:-${repo_root}/build/retrowave}"
timeout_seconds="${RETROWAVE_SMOKE_TIMEOUT:-12}"
key_delay="${RETROWAVE_SMOKE_KEY_DELAY:-1}"

if [[ ! -x "${binary}" ]]; then
  echo "RetroWave binary is missing or not executable: ${binary}" >&2
  exit 1
fi

tmp_dir="$(mktemp -d)"
cleanup() {
  rm -rf "${tmp_dir}"
}
trap cleanup EXIT

python3 "${repo_root}/scripts/smoke/generate-test-tone.py" "${tmp_dir}/tone-a.wav"
cp "${tmp_dir}/tone-a.wav" "${tmp_dir}/tone-b.wav"
cat > "${tmp_dir}/playlist.m3u" <<EOF
#EXTM3U
#EXTINF:1,Smoke Tone A
${tmp_dir}/tone-a.wav
#EXTINF:1,Smoke Tone B
${tmp_dir}/tone-b.wav
EOF

python3 "${repo_root}/scripts/smoke/tui-driver.py" \
  --timeout "${timeout_seconds}" \
  --delay "${key_delay}" \
  --keys "nq" \
  -- "${binary}" "${tmp_dir}/playlist.m3u"
