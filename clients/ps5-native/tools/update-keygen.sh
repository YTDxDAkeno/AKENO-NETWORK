#!/usr/bin/env bash
# Creates the AKENO NETWORK Native update signing key pair (docs/PS5_NATIVE_UPDATER.md, "Signing key").
#
#   clients/ps5-native/tools/update-keygen.sh <directory outside the repository>
#
# Writes <dir>/akeno-native-update-private.pem (keep it secret: store it as the GitHub Actions
# secret AKN_UPDATE_SIGNING_KEY, then keep an offline backup and delete other copies) and
# <dir>/akeno-native-update-public.pem, and rewrites src/core/update_key.h with the public key.
# Commit only update_key.h. Losing the private key means every installed version must be
# updated by hand once with a build carrying a new public key.
set -euo pipefail
dir="${1:?usage: update-keygen.sh <directory outside the repository>}"
here="$(cd "$(dirname "$0")/.." && pwd)"
repo="$(git -C "$here" rev-parse --show-toplevel 2>/dev/null || echo "$here")"
abs="$(realpath -m "$dir")"
case "$abs/" in "$repo"/*) echo "Refusing: $abs is inside the repository; the private key must never be committed." >&2; exit 1 ;; esac
mkdir -p "$abs"
private="$abs/akeno-native-update-private.pem"
public="$abs/akeno-native-update-public.pem"
[[ -e "$private" ]] && { echo "Refusing: $private exists (rotate keys deliberately)." >&2; exit 1; }
umask 077
openssl ecparam -name prime256v1 -genkey -noout | openssl pkcs8 -topk8 -nocrypt -out "$private"
openssl pkey -in "$private" -pubout -out "$public"
{
  cat <<'HDR'
/*
 * The public key that signs AKENO NETWORK Native updates (docs/PS5_NATIVE_UPDATER.md, "Signing
 * key"). Written by clients/ps5-native/tools/update-keygen.sh; the private key never enters the
 * repository. Without AKN_UPDATE_PUBLIC_KEY_PEM the updater still checks GitHub and shows
 * releases, but refuses to install anything.
 */
#ifndef AKN_UPDATE_KEY_H
#define AKN_UPDATE_KEY_H

#define AKN_UPDATE_PUBLIC_KEY_PEM \
HDR
  sed 's/^/    "/; s/$/\\n" \\/' "$public"
  echo '    ""'
  echo
  echo '#endif'
} > "$here/src/core/update_key.h"
echo "Private key: $private (secret)"
echo "Public key:  $public"
echo "Updated:     $here/src/core/update_key.h (commit this file)"
