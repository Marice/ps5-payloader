#!/usr/bin/env bash
# PayLoader - Runs a command in the build container with the project mounted at the same path.
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
exec docker run --rm -u "$(id -u):$(id -g)" -v "$root:$root" -w "$root" -e HOME="$root/.home" \
    -e CCACHE_DIR="$root/.home/ccache" payloader-build bash -lc "$*"
