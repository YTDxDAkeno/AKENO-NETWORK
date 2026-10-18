#!/usr/bin/env bash
# Fetches the pinned OnionHEN plugin SDK (GPLv3) for the experimental AKENO In-Game Companion into
# clients/ps5-ingame/third_party/onion-sdk and checks out exactly the reviewed commit. Nothing from
# it is executed except its C runtime, which is compiled into the plugin ELF, and its descriptor
# checker (tools/inspect_plugin.py), which only reads the built ELF. The native client's inputs
# (stb_image for the shared core, mbedTLS and the CA bundle via clients/ps5) are fetched too.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest="${AKENO_INGAME_THIRD_PARTY_DIR:-$here/third_party}"
mkdir -p "$dest"

ONION_SDK_URL=https://github.com/OnionBuddies/onionHEN-plugin-sdk.git
ONION_SDK_COMMIT=72527c332bc967b65f3d6401184ac81c28ae21e1 # 2026-09-03, reviewed in docs/IN_GAME_ACCESS.md

if [[ ! -d "$dest/onion-sdk/.git" ]]; then
  rm -rf "$dest/onion-sdk"
  git clone -q --filter=blob:none "$ONION_SDK_URL" "$dest/onion-sdk"
fi
git -C "$dest/onion-sdk" -c advice.detachedHead=false checkout -q --detach "$ONION_SDK_COMMIT"
test "$(git -C "$dest/onion-sdk" rev-parse HEAD)" = "$ONION_SDK_COMMIT"
test -z "$(git -C "$dest/onion-sdk" status --porcelain)" # unmodified

"$here/../ps5-native/scripts/fetch-deps.sh"
echo "In-game companion inputs ready in $dest"
