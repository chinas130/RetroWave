#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "usage: $0 <version> <install-prefix> <output-dir>" >&2
  exit 1
fi

version="$1"
install_prefix="$2"
output_dir="$3"

case "$(uname -s)" in
  Darwin)
    platform="macos"
    ;;
  Linux)
    platform="linux"
    ;;
  *)
    platform="$(uname -s | tr '[:upper:]' '[:lower:]')"
    ;;
esac

case "$(uname -m)" in
  x86_64 | amd64)
    arch="x86_64"
    ;;
  arm64 | aarch64)
    arch="arm64"
    ;;
  *)
    arch="$(uname -m)"
    ;;
esac

package_name="retrowave-${version}-${platform}-${arch}"
package_root="${output_dir}/${package_name}"
archive_path="${output_dir}/${package_name}.tar.gz"
sha_path="${output_dir}/${package_name}.sha256"

rm -rf "${package_root}" "${archive_path}" "${sha_path}"
mkdir -p "${package_root}" "${output_dir}"
cp -R "${install_prefix}/." "${package_root}/"

tar -C "${output_dir}" -czf "${archive_path}" "${package_name}"
(
  cd "${output_dir}"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "${package_name}.tar.gz" > "${package_name}.sha256"
  else
    shasum -a 256 "${package_name}.tar.gz" > "${package_name}.sha256"
  fi
)

printf '%s\n' "${archive_path}"
