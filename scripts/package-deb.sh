#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
# Binary artifacts are labelled for the actual release target, never silently relabel another OS.
source /etc/os-release
if [[ "$ID" != ubuntu || "$VERSION_ID" != 24.04 || "$(dpkg --print-architecture)" != amd64 ]]; then
  echo "This package target requires Ubuntu 24.04 amd64. Build in a matching Ubuntu environment." >&2
  exit 1
fi
sdk_dir="${SONY_SDK_DIR:?Set SONY_SDK_DIR to the extracted Sony SDK root (app/ and external/)}"
package_build="${1:-$project_dir/build-package}"
mkdir -p "$package_build"
package_build="$(cd -- "$package_build" && pwd)"
license_file="${SONY_SDK_LICENSE_FILE:-$package_build/sony-sdk-license.html}"
if [[ ! -s "$license_file" ]]; then
  python3 - "$license_file" <<'PY'
import sys
from pathlib import Path
from urllib.request import urlopen
url = 'https://support.d-imaging.sony.co.jp/app/sdk/licenseagreement/en.html'
with urlopen(url, timeout=30) as response:
    data = response.read()
if b'GRANT OF LICENSE' not in data:
    raise SystemExit('Unexpected Sony license response; use SONY_SDK_LICENSE_FILE')
Path(sys.argv[1]).write_bytes(data)
PY
fi
cmake -S "$project_dir" -B "$package_build" \
  -DSONSHELL_BUILD_BACKEND=ON -DSONSHELL_HEADLESS=ON \
  -DSONY_SDK_DIR="$sdk_dir" -DSONY_SDK_LICENSE_FILE="$license_file" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib
cmake --build "$package_build" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
ctest --test-dir "$package_build" --output-on-failure
(cd -- "$package_build" && cpack -G DEB)
