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

### onionHEN v0.9.0 startup behavior (source-verified, hardware-unverified)

The v0.9.0 host at `aydencharles/onionHEN@aaa4264` **does not**
interpret `ONION_PLUGIN_FLAG_AUTO_START` from the ELF. Its descriptor
inspector rejects that bit. The host instead discovers the ELF in its
plugins directory and exposes **Run** and **Auto Start** toggles in the
Toolbox plugin configuration. On enabling Auto Start, the host records a
marker next to the ELF: `/data/OnionHEN/plugins/AKEN00001.elf.auto_start`.
Only plugins with that marker are started by the startup reconciliation
path. With no marker, the owner would have to start the plugin manually
from the host's plugin settings.

Evidence: `source/libonion_plugin_manager/include/onion/plugin_manager.hpp`,
`source/libonion_plugin_manager/source/plugin_manager.cpp`,
`source/shellui/src/external_plugin_ui.cpp` and
`source/shellui/src/plugin_sprx_pages.cpp` at the pinned onionHEN commit.

These are **research findings, not installation instructions**. Do not
install the ELF, enable the plugin, or change HEN frameworks until the
onionHEN 12.20 compatibility and system-partition write decisions are
resolved and the console owner separately approves a hardware test.
The CI build now checks the final ELF descriptor against the host's known
flag mask and IPC/UI capability mask, as well as the SDK's own inspector.
