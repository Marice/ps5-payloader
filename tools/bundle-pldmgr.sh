#!/usr/bin/env bash
# PayLoader - Puts the latest Payload Manager (pldmgr) in the app, so PayLoader can start it.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: tools/bundle-pldmgr.sh [tag]
#
# Downloads pldmgr's release ELF from github.com/itsPLK/ps5-payload-manager (the
# latest, or the given tag) with gh, checks it is an ELF file, and writes
# assets/payload/pldmgr.elf and assets/payload/pldmgr.txt (version, SHA-256,
# source). pldmgr is GPL-3.0; its source for that version is the tag on GitHub.

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
repo=itsPLK/ps5-payload-manager
tag=${1:-$(gh release view -R "$repo" --json tagName --jq .tagName)}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

gh release download "$tag" -R "$repo" -p 'pldmgr*.elf' -D "$work"
elf=$(find "$work" -name 'pldmgr*.elf' | head -1)
[[ -n $elf ]] || { echo "bundle-pldmgr.sh: no pldmgr ELF in $repo $tag" >&2; exit 1; }
[[ $(head -c 4 "$elf" | od -An -tx1 | tr -d ' ') == 7f454c46 ]] || {
    echo "bundle-pldmgr.sh: $(basename "$elf") is not an ELF file" >&2; exit 1;
}
mkdir -p "$root/assets/payload"
cp "$elf" "$root/assets/payload/pldmgr.elf"
sum=$(sha256sum "$root/assets/payload/pldmgr.elf" | cut -d' ' -f1)
printf 'version=%s\nsha256=%s\nsource=https://github.com/%s/tree/%s\n' "${tag#v}" "$sum" "$repo" "$tag" \
    >"$root/assets/payload/pldmgr.txt"
echo "bundled pldmgr $tag ${sum:0:16}... ($(stat -c %s "$elf") bytes)"
