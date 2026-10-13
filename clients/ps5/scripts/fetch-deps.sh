#!/usr/bin/env bash
# Downloads the pinned third-party inputs of the console build into clients/ps5/third_party/ and
# verifies their SHA-256 checksums. Nothing is executed from the downloads; a checksum mismatch
# aborts before anything is extracted.
#
#   mbedTLS 3.6.7 (LTS)      Apache-2.0  TLS 1.2/1.3 and X.509 verification for the console
#                                        transport. The release tarball's library/ and include/
#                                        are identical to upstream tag v3.6.7 (checked by hand).
#   Mozilla CA bundle        MPL-2.0     Trusted roots shipped inside the console app, taken from
#                                        certifi 2026.07.22 (commit f4bc676b).
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest="${AKENO_THIRD_PARTY_DIR:-$here/third_party}"
mkdir -p "$dest"

MBEDTLS_VERSION=3.6.7
MBEDTLS_URL="https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-${MBEDTLS_VERSION}/mbedtls-${MBEDTLS_VERSION}.tar.bz2"
MBEDTLS_SHA256=a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6

CA_URL="https://raw.githubusercontent.com/certifi/python-certifi/f4bc676bc101fe2235846e37044e8c693d6cbaf4/certifi/cacert.pem"
CA_SHA256=9cc2a774b5198dcff14d9be1e66091f538975d867ce029a96bce15a55dfd730f

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

fetch "$MBEDTLS_URL" "$MBEDTLS_SHA256" "$dest/mbedtls-${MBEDTLS_VERSION}.tar.bz2"
if [[ ! -f "$dest/mbedtls/.akeno-version" ]] || [[ "$(cat "$dest/mbedtls/.akeno-version")" != "$MBEDTLS_SHA256" ]]; then
  rm -rf "$dest/mbedtls" "$dest/mbedtls-${MBEDTLS_VERSION}"
  tar -xjf "$dest/mbedtls-${MBEDTLS_VERSION}.tar.bz2" -C "$dest" "mbedtls-${MBEDTLS_VERSION}/include" \
    "mbedtls-${MBEDTLS_VERSION}/library" "mbedtls-${MBEDTLS_VERSION}/LICENSE"
  mv "$dest/mbedtls-${MBEDTLS_VERSION}" "$dest/mbedtls"
  echo "$MBEDTLS_SHA256" > "$dest/mbedtls/.akeno-version"
fi

fetch "$CA_URL" "$CA_SHA256" "$dest/cacert.pem"

echo "Third-party inputs ready in $dest"
