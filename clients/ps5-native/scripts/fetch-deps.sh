#!/usr/bin/env bash
# Downloads the pinned third-party inputs of AKENO NETWORK Native into clients/ps5-native/third_party/
# and verifies their SHA-256 checksums before anything is extracted. Nothing is executed from the
# downloads. mbedTLS and the CA bundle are shared with the daemon (clients/ps5/scripts/fetch-deps.sh).
#
#   stb_truetype.h v1.26     MIT or public domain  font rasteriser, nothings/stb 6e9f34d (2024-07-15)
#   stb_image.h v2.29        MIT or public domain  JPEG/PNG decoder (profile pictures, artwork), same commit
#   Inter 4.1                SIL OFL 1.1           the website's UI font (Regular, Bold); release zip
#   mbedTLS 3.6.7, CA bundle (via clients/ps5)     TLS for the existing client library
#   libopus 1.5.2            BSD-3-Clause          voice codec (party voice), xiph/opus release tarball
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest="${AKENO_NATIVE_THIRD_PARTY_DIR:-$here/third_party}"
mkdir -p "$dest"

STB_COMMIT=6e9f34d5429cf16790ec43c9bac3f1ee4ad1f760
STB_URL="https://raw.githubusercontent.com/nothings/stb/${STB_COMMIT}/stb_truetype.h"
STB_SHA256=ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab
STB_IMAGE_URL="https://raw.githubusercontent.com/nothings/stb/${STB_COMMIT}/stb_image.h"
STB_IMAGE_SHA256=c54b15a689e6a1f32c75e2ec23afa442e3e0e37e894b73c1974d08679b20dd5c

INTER_VERSION=4.1
INTER_URL="https://github.com/rsms/inter/releases/download/v${INTER_VERSION}/Inter-${INTER_VERSION}.zip"
INTER_SHA256=9883fdd4a49d4fb66bd8177ba6625ef9a64aa45899767dde3d36aa425756b11e
INTER_REGULAR_SHA256=40d692fce188e4471e2b3cba937be967878f631ad3ebbbdcd587687c7ebe0c82
INTER_BOLD_SHA256=288316099b1e0a47a4716d159098005eef7c0066921f34e3200393dbdb01947f
INTER_LICENSE_SHA256=262481e844521b326f5ecd053e59b98c8b2da78c8ee1bdbb6e8174305e54935a

OPUS_VERSION=1.5.2
OPUS_URL="https://github.com/xiph/opus/releases/download/v${OPUS_VERSION}/opus-${OPUS_VERSION}.tar.gz"
OPUS_SHA256=65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1

fetch() { # url sha256 output
  local url="$1" sum="$2" out="$3"
  if [[ -f "$out" ]] && echo "$sum  $out" | sha256sum -c --status; then
    return 0
  fi
  rm -f "$out.part"
  curl -sSfL --retry 4 --retry-delay 2 -o "$out.part" "$url"
  if ! echo "$sum  $out.part" | sha256sum -c --status; then
    echo "Checksum mismatch for $url" >&2
    echo "  expected $sum" >&2
    echo "  got      $(sha256sum "$out.part" | cut -d' ' -f1)" >&2
    rm -f "$out.part"
    exit 1
  fi
  mv "$out.part" "$out"
}

verify() { # sha256 file
  if ! echo "$1  $2" | sha256sum -c --status; then
    echo "Checksum mismatch for $2" >&2
    exit 1
  fi
}

fetch "$STB_URL" "$STB_SHA256" "$dest/stb_truetype.h"
fetch "$STB_IMAGE_URL" "$STB_IMAGE_SHA256" "$dest/stb_image.h"

fetch "$INTER_URL" "$INTER_SHA256" "$dest/Inter-${INTER_VERSION}.zip"
mkdir -p "$dest/inter"
if ! { echo "$INTER_REGULAR_SHA256  $dest/inter/Inter-Regular.ttf" && echo "$INTER_BOLD_SHA256  $dest/inter/Inter-Bold.ttf" &&
       echo "$INTER_LICENSE_SHA256  $dest/inter/OFL.txt"; } | sha256sum -c --status 2>/dev/null; then
  tmp="$(mktemp -d)"
  unzip -q -o "$dest/Inter-${INTER_VERSION}.zip" extras/ttf/Inter-Regular.ttf extras/ttf/Inter-Bold.ttf LICENSE.txt -d "$tmp"
  mv "$tmp/extras/ttf/Inter-Regular.ttf" "$tmp/extras/ttf/Inter-Bold.ttf" "$dest/inter/"
  mv "$tmp/LICENSE.txt" "$dest/inter/OFL.txt"
  rm -rf "$tmp"
fi
verify "$INTER_REGULAR_SHA256" "$dest/inter/Inter-Regular.ttf"
verify "$INTER_BOLD_SHA256" "$dest/inter/Inter-Bold.ttf"
verify "$INTER_LICENSE_SHA256" "$dest/inter/OFL.txt"

fetch "$OPUS_URL" "$OPUS_SHA256" "$dest/opus-${OPUS_VERSION}.tar.gz"
if [[ ! -f "$dest/opus/.akeno-opus-${OPUS_VERSION}" ]]; then
  rm -rf "$dest/opus" "$dest/opus-${OPUS_VERSION}"
  tar -xzf "$dest/opus-${OPUS_VERSION}.tar.gz" -C "$dest"
  mv "$dest/opus-${OPUS_VERSION}" "$dest/opus"
  touch "$dest/opus/.akeno-opus-${OPUS_VERSION}"
fi

# The client library's TLS inputs (same pinned files as the daemon build).
"$here/../ps5/scripts/fetch-deps.sh"

echo "Native third-party inputs ready in $dest"
