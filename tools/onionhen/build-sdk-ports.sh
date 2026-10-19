#!/usr/bin/env bash
# Installs the SDK ports that onionHEN's util component links (libcurl with OpenSSL, zlib,
# libpsl and zstd, plus a CA bundle) into $PS5_PAYLOAD_SDK/target/user/homebrew, the place
# source/util/CMakeLists.txt looks for them. It follows the ps5-payload-dev/pacbrew-repo recipes
# (zlib, openssl, libpsl, zstd and curl PKGBUILDs at 722187f) step by step, without
# makepkg/pacman, at the same versions and with the same SHA-256 sums. PC only; nothing is sent to a console.
#
#   PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk tools/onionhen/build-sdk-ports.sh
#
# The CA bundle is Mozilla's, taken from a pinned certifi wheel (curl's mk-ca-bundle.pl fetches
# the same data unpinned). Never from the build machine's trust store.
set -euo pipefail

sdk="${PS5_PAYLOAD_SDK:?PS5_PAYLOAD_SDK must point to an installed ps5-payload-sdk}"
work="${PORTS_WORK:-${TMPDIR:-/tmp}/akeno-sdk-ports}"
jobs="${JOBS:-$(nproc)}"
mkdir -p "$work"

fetch() { # url sha256 file
  local file="$work/$3"
  [[ -f "$file" ]] && echo "$2  $file" | sha256sum -c --quiet - 2> /dev/null && return
  curl -fsSL --retry 3 -o "$file" "$1"
  echo "$2  $file" | sha256sum -c --quiet -
}

unpack() { # file dir
  rm -rf "${work:?}/$2"
  tar xf "$work/$1" -C "$work"
}

fetch https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.gz \
  bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16 zlib-1.3.2.tar.gz
fetch https://github.com/openssl/openssl/releases/download/openssl-3.5.2/openssl-3.5.2.tar.gz \
  c53a47e5e441c930c3928cf7bf6fb00e5d129b630e0aa873b08258656e7345ec openssl-3.5.2.tar.gz
fetch https://github.com/rockdaboot/libpsl/releases/download/0.21.5/libpsl-0.21.5.tar.gz \
  1dcc9ceae8b128f3c0b3f654decd0e1e891afc6ff81098f227ef260449dae208 libpsl-0.21.5.tar.gz
fetch https://github.com/facebook/zstd/releases/download/v1.5.6/zstd-1.5.6.tar.gz \
  8c29e06cf42aacc1eafc4077ae2ec6c6fcb96a626157e0593d5e82a34fd403c1 zstd-1.5.6.tar.gz
fetch https://github.com/curl/curl/releases/download/curl-8_18_0/curl-8.18.0.tar.xz \
  40df79166e74aa20149365e11ee4c798a46ad57c34e4f68fd13100e2c9a91946 curl-8.18.0.tar.xz
fetch https://files.pythonhosted.org/packages/0b/a7/71ac2cff56fec219ed242bb11b8efb69fcc4bec75db06fb7bfe35de520e6/certifi-2026.7.22-py3-none-any.whl \
  62f22742b58a1a33014a2b6b706588a8d7e2a88ae7bd1a6ebe8c992928483775 certifi-2026.7.22-py3-none-any.whl

# Each port in a subshell with the SDK's cross environment (CC, PREFIX=/user/homebrew,
# DESTDIR=<sdk>/target), as makepkg would run build() and package().
(
  source "$sdk/toolchain/prospero.sh"
  unpack zlib-1.3.2.tar.gz zlib-1.3.2
  cd "$work/zlib-1.3.2"
  ./configure --prefix="$PREFIX" --static
  $MAKE -j"$jobs"
  $MAKE install DESTDIR="$DESTDIR"
  "$PS5_CROSS_FIX_ROOT" "$DESTDIR/$PREFIX"
)
(
  source "$sdk/toolchain/prospero.sh"
  unpack openssl-3.5.2.tar.gz openssl-3.5.2
  cd "$work/openssl-3.5.2"
  ./Configure BSD-x86_64 no-tests no-apps no-shared --prefix="$PREFIX"
  $MAKE -j"$jobs" build_sw
  $MAKE install_sw DESTDIR="$DESTDIR"
  "$PS5_CROSS_FIX_ROOT" "$DESTDIR/$PREFIX"
)
(
  source "$sdk/toolchain/prospero.sh"
  unpack libpsl-0.21.5.tar.gz libpsl-0.21.5
  cd "$work/libpsl-0.21.5"
  ./configure --prefix="$PREFIX" --host=x86_64-pc-freebsd --enable-static --disable-shared \
    --disable-nls --disable-gtk-doc-html
  $MAKE -j"$jobs"
  $MAKE install DESTDIR="$DESTDIR"
  "$PS5_CROSS_FIX_ROOT" "$DESTDIR/$PREFIX"
)
(
  source "$sdk/toolchain/prospero.sh"
  unpack zstd-1.5.6.tar.gz zstd-1.5.6
  cd "$work/zstd-1.5.6"
  $CMAKE -DCMAKE_BUILD_TYPE=Release -DZSTD_BUILD_STATIC=YES -DZSTD_BUILD_SHARED=NO \
    -B builddir -S build/cmake
  $MAKE -C builddir -j"$jobs"
  $MAKE -C builddir install
  "$PS5_CROSS_FIX_ROOT" "$DESTDIR/$PREFIX"
)
(
  source "$sdk/toolchain/prospero.sh"
  unpack curl-8.18.0.tar.xz curl-8.18.0
  cd "$work/curl-8.18.0"
  sed -i 's|define USE_XATTR| |g' src/tool_xattr.h
  autoreconf -fi
  ./configure --prefix="$PREFIX" --host=x86_64-pc-freebsd --enable-static --disable-shared \
    --with-openssl --with-ca-bundle="$PREFIX/etc/ca-bundle.crt" --disable-docs
  $MAKE -j"$jobs"
  $MAKE install DESTDIR="$DESTDIR"
  "$PS5_CROSS_FIX_ROOT" "$DESTDIR/$PREFIX"
  mkdir -p "$DESTDIR/$PREFIX/etc"
  python3 -I -c 'import sys, zipfile; sys.stdout.buffer.write(zipfile.ZipFile(sys.argv[1]).read("certifi/cacert.pem"))' \
    "$work/certifi-2026.7.22-py3-none-any.whl" > "$DESTDIR/$PREFIX/etc/ca-bundle.crt"
)

root="$sdk/target/user/homebrew"
test -f "$root/include/curl/curl.h"
test -f "$root/lib/libcurl.a"
grep -q 'BEGIN CERTIFICATE' "$root/etc/ca-bundle.crt"
echo "SDK ports installed in $root: $(PKG_CONFIG_PATH="$root/lib/pkgconfig" "$sdk/bin/prospero-pkg-config" --modversion libcurl 2> /dev/null || echo curl) with OpenSSL, zlib, libpsl, zstd"
