# Rollback — AKENO NETWORK Native 1.0.0-beta.1

Nothing in this release changes pairing, settings, the server, accounts, system files, games or
the HEN environment, so going back means only replacing files you copied.

## AKENO Native

| Go back to | Do this | Status |
| --- | --- | --- |
| **Native 0.1** (recommended) | replace `/data/homebrew/AKENONative/` with `fallback/native-0.1-d094e62bcaf1/AKENONative/` | **hardware-tested** on FW 12.20 (`docs/PS5_NATIVE_HARDWARE_RESULTS_FW12.20.md`) |
| Native 0.2 preview | replace it with `fallback/native-0.2-preview-4b4602c7d0b7/AKENONative/` and use that folder's IPC-1 daemon | partly hardware-verified (owner report: profiles and activity on FW 12.20); not fully tested |
| nothing | delete `/data/homebrew/AKENONative/` (and optionally `/data/akeno-native/`) | — |

## The daemon

1. Open the AKENO page and press **Exit AKENO** (confirm). The native-IPC daemon deletes
   `/data/akeno/native.key` when it stops.
2. Send `fallback/akeno-network-ps5-alpha-0.5-rc-7e28255.elf` (the **hardware-tested** Alpha 0.5
   RC, byte-identical to its CI build) or the release ELF you used before.

If `native.key` was left behind (crash, power loss) it is harmless — the release daemon ignores
it — and you may delete it.

## The experimental In-Game Companion

Delete `/data/OnionHEN/plugins/AKEN00001.elf`. Removing OnionHEN itself is described in
`docs/IN_GAME_ACCESS.md` (it is not part of this release and is never installed by AKENO).

## Check the files you go back to

```sh
sha256sum -c SHA256SUMS
```

The fallback files are rebuilt from their own commits in every CI run and must reproduce the
recorded checksums (`BUILDINFO.txt`), so they are the same bytes that were tested.
