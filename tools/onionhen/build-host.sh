#!/usr/bin/env bash
# Builds the EXPERIMENTAL "onionHEN v0.9.0 + AKENO direct entry" host: the pinned upstream onionHEN
# (aydencharles/onionHEN tag v0.9.0, aaa4264) with the patch series PATCHES (Phase 4's
# akeno-direct-entry.patch, then Phase 4.1's akeno-direct-entry-fix.patch), using onionHEN's own build
# pipeline (scripts/build.sh --release). PC only; nothing is sent to a console.
# docs/SHELLUI_DIRECT_ENTRY.md says what the patches change and why.
#
#   PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk tools/onionhen/build-host.sh <out dir> [--baseline]
#
# The SDK needs libc++ (the SDK's libcxx.sh) and the ports of build-sdk-ports.sh.
#   <out dir>/OnionHEN-AKENO-direct-fixed.elf   the patched host (Phase 4.1)
#   <out dir>/shellui.sym.elf             its ShellUI payload with symbols (for the hook offset)
#   <out dir>/host-tests.txt              onionHEN's own host test suite on the patched tree
# With --baseline it builds the UNPATCHED upstream instead (<out dir>/OnionHEN-v0.9.0-rebuilt.elf),
# to compare this toolchain with the official release binary.
set -euo pipefail

ONIONHEN_URL=https://github.com/aydencharles/onionHEN.git
ONIONHEN_COMMIT=aaa4264588e00da33faccc7642d27c3c4cc339b1 # tag v0.9.0
KSTUFF_SHA256=ab9a6cb4d3b1daf139d4d646e402b1cf569071acd64599c936d7a3a6164dc779 # kstuff-lite v1.11, what build.sh downloads
PATCHES=(akeno-direct-entry.patch akeno-direct-entry-fix.patch) # applied in this order

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="$(mkdir -p "${1:?out dir}" && cd "$1" && pwd)"
baseline=0
[[ "${2:-}" == "--baseline" ]] && baseline=1
sdk="${PS5_PAYLOAD_SDK:?PS5_PAYLOAD_SDK must point to an installed ps5-payload-sdk}"
llvm="${LLVM_BIN:-/usr/lib/llvm-18/bin}"
work="${HOST_WORK:-${TMPDIR:-/tmp}/akeno-onionhen-host}"
src="$work/onionhen"

test -f "$sdk/target/lib/libc++.a" || { echo "SDK without libc++: run the SDK's libcxx.sh" >&2; exit 1; }
test -f "$sdk/target/user/homebrew/lib/libcurl.a" || { echo "SDK without ports: run build-sdk-ports.sh" >&2; exit 1; }

rm -rf "$src"
git clone -q "$ONIONHEN_URL" "$src"
git -C "$src" -c advice.detachedHead=false checkout -q --detach "$ONIONHEN_COMMIT"
test "$(git -C "$src" rev-parse HEAD)" = "$ONIONHEN_COMMIT"
git -C "$src" submodule update --init -q
if [[ $baseline == 0 ]]; then
  for patch in "${PATCHES[@]}"; do git -C "$src" apply --index "$here/$patch"; done
  git -C "$src" diff --cached --stat
fi

export PS5_PAYLOAD_SDK="$sdk" LLVM_CONFIG="${LLVM_CONFIG:-$llvm/llvm-config}"
export PATH="$llvm:$sdk/bin:$PATH" ONIONHEN_CACHE_DIR="$work/cache"
(cd "$src" && ./scripts/build.sh --release --jobs "${JOBS:-$(nproc)}")
test "$(sha256sum "$work/cache/kstuff.elf" | cut -d' ' -f1)" = "$KSTUFF_SHA256"

if [[ $baseline == 1 ]]; then
  cp "$src/build/bin/OnionHEN.elf" "$out/OnionHEN-v0.9.0-rebuilt.elf"
  sha256sum "$out/OnionHEN-v0.9.0-rebuilt.elf"
  exit 0
fi
cp "$src/build/bin/OnionHEN.elf" "$out/OnionHEN-AKENO-direct-fixed.elf"

# shellui.elf once more without stripping, from the same objects; its .text must be identical.
link="$(cd "$src/build" && ninja -t commands shellui | tail -1 | sed 's/ && cd .*//; s#-o bin/shellui.elf#-o bin/shellui.sym.elf#')"
(cd "$src/build" && eval "${link#: && }") 2> /dev/null
cmp <("$llvm/llvm-objcopy" -O binary --only-section=.text "$src/build/bin/shellui.sym.elf" /dev/stdout) \
    <("$llvm/llvm-objcopy" -O binary --only-section=.text "$src/build/bin/shellui.elf" /dev/stdout)
cp "$src/build/bin/shellui.sym.elf" "$out/shellui.sym.elf"

# onionHEN's own host tests on the patched tree (they include the patch's new cases).
if [[ -n "${KEYSTONE_PREFIX:-}" ]]; then
  LD_LIBRARY_PATH="$KEYSTONE_PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    make -C "$src/source/util/tests" -j"${JOBS:-$(nproc)}" test KEYSTONE_PREFIX="$KEYSTONE_PREFIX" \
    > "$out/host-tests.log" 2>&1
  grep -E '^\[(PASS|FAIL)\]|All util host tests passed' "$out/host-tests.log" > "$out/host-tests.txt"
  grep -q 'All util host tests passed' "$out/host-tests.txt"
  ! grep -q '^\[FAIL\]' "$out/host-tests.txt"
fi
sha256sum "$out/OnionHEN-AKENO-direct-fixed.elf"
