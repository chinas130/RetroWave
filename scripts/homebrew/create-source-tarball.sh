#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "usage: $0 <tag-or-ref> [output-dir]" >&2
  exit 1
fi

ref="$1"
output_dir="${2:-dist}"
version="${ref#v}"
archive_name="retrowave-${version}.tar.gz"
prefix="retrowave-${version}/"

mkdir -p "${output_dir}"
git archive --format=tar.gz --prefix="${prefix}" -o "${output_dir}/${archive_name}" "${ref}"
printf '%s\n' "${output_dir}/${archive_name}"
