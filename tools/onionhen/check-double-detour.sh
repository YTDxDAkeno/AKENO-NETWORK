#!/usr/bin/env bash
# Builds tools/onionhen/double_detour_check.cpp against the relocator of the pinned onionHEN v0.9.0
# (a sparse checkout of source/libonion_detour only) and replays a second Toolbox injection over the
# sceRegMgrGetInt patch of each recorded shellui.elf layout. PC only; nothing is sent to a console.
#
#   tools/onionhen/check-double-detour.sh [onionhen checkout]
#
# The offsets are those of sceRegMgrGetInt_hook in shellui.elf as built by
# tools/onionhen/measure-hook-offset.sh (Release, ps5-payload-sdk v0.44, clang 18):
#   original-v0.9.0   aydencharles/onionHEN@aaa4264, unmodified ShellUI sources
#   pr14-custom-host  the same after tools/shellui_host/patch_onionhen.py of PR #14
set -euo pipefail

ONIONHEN_URL=https://github.com/aydencharles/onionHEN.git
ONIONHEN_COMMIT=aaa4264588e00da33faccc7642d27c3c4cc339b1 # tag v0.9.0
ORIGINAL_OFFSET=0x318e8
PR14_OFFSET=0x2ef48

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
work="${TMPDIR:-/tmp}/akeno-onionhen-detour"
src="${1:-}"
if [[ -z "$src" ]]; then
  src="$work/onionhen"
  if [[ ! -d "$src/.git" ]]; then
    rm -rf "$src"
    git clone -q --filter=blob:none --no-checkout "$ONIONHEN_URL" "$src"
    git -C "$src" sparse-checkout set --no-cone /source/libonion_detour/
  fi
  git -C "$src" -c advice.detachedHead=false checkout -q --detach "$ONIONHEN_COMMIT"
fi
test "$(git -C "$src" rev-parse HEAD)" = "$ONIONHEN_COMMIT"
detour="$src/source/libonion_detour"

mkdir -p "$work"
"${CXX:-g++}" -std=c++20 -O1 -Wall -Wextra -I"$detour/include" -o "$work/double_detour_check" \
  "$here/double_detour_check.cpp" "$detour/source/x64_relocator.cpp" "$detour/source/hde64.cpp"
"$work/double_detour_check" --expect original-v0.9.0="$ORIGINAL_OFFSET" pr14-custom-host="$PR14_OFFSET"
