#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 4 ]]; then
  echo "usage: $0 <version> <sha256> <repository> <output-file>" >&2
  exit 1
fi

version="$1"
sha256="$2"
repository="$3"
output_file="$4"
template_file="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/packaging/homebrew/retrowave.rb.in"
tag="v${version}"
url="https://github.com/${repository}/releases/download/${tag}/retrowave-${version}.tar.gz"

mkdir -p "$(dirname "${output_file}")"
sed \
  -e "s|@VERSION@|${version}|g" \
  -e "s|@SHA256@|${sha256}|g" \
  -e "s|@REPOSITORY@|${repository}|g" \
  -e "s|@URL@|${url}|g" \
  "${template_file}" > "${output_file}"
