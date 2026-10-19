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
#   akeno-direct-host the same with tools/onionhen/akeno-direct-entry.patch (Phase 4)
#
# Second pass: the relocator of akeno-direct-entry.patch (+ the byte-wise prefix test of
# akeno-direct-entry-fix.patch) must make a repeated injection chain to
# the old hook for every recorded layout and every base (--expect-chain), so the patched host's
# behaviour no longer depends on where its build puts sceRegMgrGetInt_hook.
set -euo pipefail

ONIONHEN_URL=https://github.com/aydencharles/onionHEN.git
ONIONHEN_COMMIT=aaa4264588e00da33faccc7642d27c3c4cc339b1 # tag v0.9.0
ORIGINAL_OFFSET=0x318e8
PR14_OFFSET=0x2ef48
AKENO_DIRECT_OFFSET="${AKENO_DIRECT_OFFSET:-}" # measured by measure-hook-offset.sh (CI host job)

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

patched="$work/onionhen-akeno-direct"
rm -rf "$patched"
mkdir -p "$patched/source"
cp -a "$detour" "$patched/source/libonion_detour"
git -C "$src" diff --quiet # the pinned checkout itself stays unmodified
(cd "$patched" && for patch in akeno-direct-entry.patch akeno-direct-entry-fix.patch; do
  git apply --include='source/libonion_detour/*' "$here/$patch"; done)
! cmp -s "$detour/source/x64_relocator.cpp" "$patched/source/libonion_detour/source/x64_relocator.cpp"
"${CXX:-g++}" -std=c++20 -O1 -Wall -Wextra -I"$patched/source/libonion_detour/include" -o "$work/double_detour_check_patched" \
  "$here/double_detour_check.cpp" "$patched/source/libonion_detour/source/x64_relocator.cpp" \
  "$patched/source/libonion_detour/source/hde64.cpp"
echo "--- with the relocator of akeno-direct-entry.patch + akeno-direct-entry-fix.patch ---"
"$work/double_detour_check_patched" --expect-chain original-v0.9.0="$ORIGINAL_OFFSET" \
  pr14-custom-host="$PR14_OFFSET" ${AKENO_DIRECT_OFFSET:+akeno-direct-host="$AKENO_DIRECT_OFFSET"}
