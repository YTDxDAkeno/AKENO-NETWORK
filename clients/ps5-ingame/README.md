# AKENO In-Game Companion (EXPERIMENTAL)

An OnionHEN plugin (`AKEN00001.elf`) that adds an **AKENO NETWORK** page to the OnionHEN Toolbox:
friends online, unread messages, your party, invitations, notifications and your status, while a
game keeps running; plus two confirmed quick actions (status, accept a party invitation). It reads
everything through the AKENO daemon's native interface and needs no console system function.

**Not run on a PS5. Requires OnionHEN, which is the owner's decision.** Read
[docs/IN_GAME_ACCESS.md](../../docs/IN_GAME_ACCESS.md) first: research, safety review, install,
rollback and the hardware proof.

```sh
scripts/fetch-deps.sh                                    # pinned OnionHEN plugin SDK (GPLv3) and shared inputs
cmake -S . -B build -DAKN_SANITIZE=ON && cmake --build build && ctest --test-dir build
build/akeno-ingame-check --port <daemon port> --akeno-dir <dir>   # the menu from a running daemon (PC)
PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk make -C console && console/check-plugin.sh build-ps5
```

Sources are MIT (AKENO); the plugin ELF links the OnionHEN SDK runtime and is therefore GPLv3.

## Phase 3 compatibility fixes (software only)

The pinned plugin SDK accepts an `AUTO_START` descriptor bit and
`DISABLED` UI nodes, but Phase 2.5's host-side checks found that the
onionHEN v0.9.0 host rejects the former and its Settings renderer does
not handle the latter. This plugin now omits `AUTO_START` and renders
informational labels instead of unavailable status/invitation actions.
Regression tests inspect the encoded UI for unsupported disabled flags.

**Not yet verified on PS5 firmware 12.20:** that onionHEN discovers and
starts this descriptor, that its UI is available during gameplay, or
that this plugin reads live daemon data there. PC validation and CI are
not hardware evidence. Do not install or change HEN frameworks on the
console as part of building/testing this branch. The custom onionHEN
build and any system-partition write avoidance are separate, pending
work requiring review and console-owner approval.
