#!/usr/bin/env bash
# Prints the offset of sceRegMgrGetInt_hook in an onionHEN shellui.elf: configures an onionHEN
# checkout with the PS5 payload SDK, builds only the shellui target (Release) and relinks it once
# without stripping, so the symbol can be read; the stripped file's .text must be identical.
# PC only. The checkout must already have whatever source patch is being measured applied.
#
#   PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk tools/onionhen/measure-hook-offset.sh <onionhen checkout>
set -euo pipefail
src="${1:?onionHEN checkout}"
sdk="${PS5_PAYLOAD_SDK:?PS5_PAYLOAD_SDK must point to an installed ps5-payload-sdk}"
build="$src/build-akeno-measure"
llvm="${LLVM_BIN:-/usr/lib/llvm-18/bin}"

# kstuff is not built here (a stub path is enough to configure). onionHEN's util component needs
# libcurl in the SDK to configure; SDK v0.44 has none. util is not part of shellui.elf, so for an
# unmodified checkout replace only source/util/CMakeLists.txt with PR #14's patched one (the ShellUI
# sources stay untouched); that is how the recorded original offset was measured.
"$sdk/bin/prospero-cmake" -S "$src/source" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DONIONHEN_KSTUFF_ELF="$build/kstuff-not-built.elf" -DPS5_PAYLOAD_SDK="$sdk" > "$build.configure.log" 2>&1
cmake --build "$build" --target shellui > "$build.build.log" 2>&1
link="$(cd "$build" && ninja -t commands shellui | tail -1 | sed 's/ && cd .*//; s#-o bin/shellui.elf#-o bin/shellui.sym.elf#')"
(cd "$build" && eval "${link#: && }") 2> /dev/null
cmp <("$llvm/llvm-objcopy" -O binary --only-section=.text "$build/bin/shellui.sym.elf" /dev/stdout) \
    <("$llvm/llvm-objcopy" -O binary --only-section=.text "$build/bin/shellui.elf" /dev/stdout)
"$llvm/llvm-nm" "$build/bin/shellui.sym.elf" | awk '/ T _Z20sceRegMgrGetInt_hooklPi$/ { print "0x" $1 }' | sed 's/^0x0*/0x/'
