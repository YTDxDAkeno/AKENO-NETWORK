#!/usr/bin/env bash
# Builds SDL2 for the PS5 from ps5-payload-dev/SDL at a pinned commit, as a static library with
# the PS5 video, audio and game-controller drivers and the software renderer only (no OpenGL,
# Vulkan or Mesa: the software renderer is what works for homebrew in the foreground, see
# docs/PS5_NATIVE_ADR.md). The result goes to a prefix that console/Makefile uses.
#
#   export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk      # pinned SDK, see .github/workflows/ps5-native.yml
#   clients/ps5-native/scripts/build-sdl-ps5.sh [prefix]   # default: clients/ps5-native/third_party/sdl-ps5
set -euo pipefail

SDL_REPO=https://github.com/ps5-payload-dev/SDL.git
SDL_COMMIT=ee4c47dc0d617b3bc8f35108f9956baf228a1322 # release-2.30.x-ps5, 2026-09-24 (zlib licence)
SDL_CONFIG_ID="$SDL_COMMIT file-prefix-map"

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${PS5_PAYLOAD_SDK:?PS5_PAYLOAD_SDK must point to the installed ps5-payload-sdk}"
prefix="${1:-$here/third_party/sdl-ps5}"
work="${SDL_WORK_DIR:-$here/third_party/sdl-src}"

if [[ -f "$prefix/.akeno-sdl-commit" ]] && [[ "$(cat "$prefix/.akeno-sdl-commit")" == "$SDL_CONFIG_ID" ]] &&
   [[ -f "$prefix/lib/libSDL2.a" ]]; then
  echo "SDL2 $SDL_COMMIT already built in $prefix"
  exit 0
fi

if [[ ! -d "$work/.git" ]]; then
  rm -rf "$work"
  git clone --filter=blob:none "$SDL_REPO" "$work"
fi
git -C "$work" fetch --quiet origin "$SDL_COMMIT" 2>/dev/null || true
git -C "$work" checkout --quiet --detach "$SDL_COMMIT"
test "$(git -C "$work" rev-parse HEAD)" = "$SDL_COMMIT"

build="$work/build-ps5"
rm -rf "$build"
# SDL's project() also enables C++; the SDK has no C++ runtime and SDL needs none, so the C++
# compiler check is skipped. SDL's source is not modified (its dynamic API stays on, so every SDL
# function is linked; console/check-native.sh shows which of them AKENO code uses).
# -ffile-prefix-map keeps the build directory out of the library (SDL_assert records __FILE__), so
# the result is the same on every machine.
"$PS5_PAYLOAD_SDK/bin/prospero-cmake" -S "$work" -B "$build" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER_WORKS=ON -DCMAKE_C_FLAGS="-ffile-prefix-map=$work=SDL" \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF \
  -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF -DSDL_VULKAN=OFF -DSDL_LIBSAMPLERATE=OFF -DSDL_LOADSO=ON
cmake --build "$build" -j"$(nproc)"
rm -rf "$prefix"
cmake --install "$build" --prefix "$prefix" >/dev/null
config="$prefix/include/SDL2/SDL_config.h"
for define in SDL_VIDEO_DRIVER_PS5 SDL_AUDIO_DRIVER_PS5 SDL_JOYSTICK_PS5; do
  grep -q "#define $define 1" "$config" || { echo "SDL2 build lacks $define" >&2; exit 1; }
done
# The software renderer is built unless rendering is disabled; OpenGL must be off.
if grep -q "#define SDL_RENDER_DISABLED 1" "$config" || grep -q "#define SDL_VIDEO_OPENGL 1" "$config"; then
  echo "SDL2 build has an unexpected renderer configuration" >&2
  exit 1
fi
echo "$SDL_CONFIG_ID" > "$prefix/.akeno-sdl-commit"
echo "SDL2 $SDL_COMMIT installed in $prefix ($(sha256sum "$prefix/lib/libSDL2.a" | cut -d' ' -f1))"
