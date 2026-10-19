#!/usr/bin/env bash
# Opens AKEN00001's real documents through the patched host's direct entry (direct_entry_check.cpp):
# runs the companion's host tests with AKN_DUMP_DOCS so they write every document they check, then
# compiles onionHEN's plugin registry and dynamic page renderer (pinned v0.9.0 + akeno-direct-entry.patch;
# a sparse checkout of the five files involved) and replays each document. PC only.
#
#   tools/onionhen/check-direct-entry.sh <akn_ingame_tests binary> [onionhen checkout]
set -euo pipefail

ONIONHEN_URL=https://github.com/aydencharles/onionHEN.git
ONIONHEN_COMMIT=aaa4264588e00da33faccc7642d27c3c4cc339b1 # tag v0.9.0

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
tests="$(realpath "${1:?akn_ingame_tests binary}")"
work="${TMPDIR:-/tmp}/akeno-onionhen-direct-entry"
src="${2:-}"
if [[ -z "$src" ]]; then
  src="$work/onionhen"
  if [[ ! -d "$src/.git" ]]; then
    rm -rf "$src"
    git clone -q --filter=blob:none --no-checkout "$ONIONHEN_URL" "$src"
    git -C "$src" sparse-checkout set --no-cone /source/libonion_plugin_ui/ /source/shellui/include/ \
      /source/shellui/src/dynamic_ui_runtime.cpp /source/shellui/src/dynamic_ui_xml.cpp /source/shellui/src/ps5_settings_ui.cpp
  fi
  git -C "$src" -c advice.detachedHead=false checkout -q -f --detach "$ONIONHEN_COMMIT"
fi
test "$(git -C "$src" rev-parse HEAD)" = "$ONIONHEN_COMMIT"
git -C "$src" checkout -q -- . # drop an earlier run's patch
git -C "$src" apply --include='source/shellui/include/*' --include='source/shellui/src/dynamic_ui_*' \
  --include='source/libonion_plugin_ui/*' "$here/akeno-direct-entry.patch"
s="$src/source"

docs="$work/docs"
rm -rf "$docs"
mkdir -p "$docs"
AKN_DUMP_DOCS="$docs" "$tests" > "$work/companion-tests.txt"
count="$(find "$docs" -name 'doc-*.bin' | wc -l)"
test "$count" -gt 10

"${CXX:-g++}" -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all \
  -I"$s/libonion_plugin_ui/include" -I"$s/shellui/include" -o "$work/direct_entry_check" \
  "$here/direct_entry_check.cpp" "$s/libonion_plugin_ui/source/plugin_ui.cpp" "$s/shellui/src/dynamic_ui_runtime.cpp" \
  "$s/shellui/src/dynamic_ui_xml.cpp" "$s/shellui/src/ps5_settings_ui.cpp"
"$work/direct_entry_check" "$docs"/doc-*.bin | tail -n 4
