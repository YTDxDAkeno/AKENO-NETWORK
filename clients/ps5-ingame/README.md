# AKENO In-Game Companion (EXPERIMENTAL)

An onionHEN plugin (`AKEN00001.elf`, version 1.20; 1.10 is the hardware-tested rollback, rebuilt from
`f801370`) that adds **AKENO NETWORK** pages to the
onionHEN Toolbox in PS5 Settings, also reachable while a game keeps running: friends with presence
and current game, a page per friend (message them with the PS5 keyboard, open the conversation,
invite them or join their party), conversations with their latest messages, replies and Mark as
read, your party (members, chat, invite, leave; create or join), friend requests and invitations
(accept or decline), notifications, your status, and a diagnostics page. Everything goes through
the AKENO daemon's native interface; the plugin needs no console system function and writes no
file.

**Plugin 1.00 ran on the owner's PS5 (12.20) with the original onionHEN v0.9.0; 1.10 has not run
on a PS5.** Read [docs/IN_GAME_ACCESS.md](../../docs/IN_GAME_ACCESS.md) (design, page rules,
testing) and the owner guides in [docs/shellui/](../../docs/shellui/) (install, test plan,
rollback, all German). The host decision: [docs/SHELLUI_HOST_DIAGNOSIS.md](../../docs/SHELLUI_HOST_DIAGNOSIS.md).

```sh
scripts/fetch-deps.sh                                    # pinned OnionHEN plugin SDK (GPLv3) and shared inputs
cmake -S . -B build -DAKN_SANITIZE=ON && cmake --build build && ctest --test-dir build
build/akeno-ingame-check --port <daemon port> --akeno-dir <dir> --pages   # the pages from a running daemon (PC)
build/akeno-ingame-check ... --event reply.<conversation ID> "on my way"  # a press, as onionHEN delivers it
PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk make -C console && console/check-plugin.sh build-ps5
```

Sources are MIT (AKENO); the plugin ELF links the OnionHEN SDK runtime and is therefore GPLv3.

| File | What |
| --- | --- |
| `src/companion.c` | the model the pages show, built from the daemon's answers |
| `src/fetch.c` | the store: section loads, the daemon's event ring, the latest messages per conversation, writes |
| `src/ui_doc.c` | the onion.ui document: every page, within onionHEN v0.9.0's rules |
| `src/actions.c` | what a press does; node ID → verb and item |
| `src/voice_link.c` | 1.20: the voice page's link to the background voice service `akeno-party-voice` ([docs/PS5_PARTY_VOICE.md](../../docs/PS5_PARTY_VOICE.md)) |
| `src/plugin_main.c` | the plugin process: onionHEN link, refresh loop, notifications, tombstone pages |
| `src/check_main.c` | the PC check tool used by the end-to-end test |

## Phase 3 compatibility fixes (software only)

The pinned plugin SDK accepts an `AUTO_START` descriptor bit and
`DISABLED` UI nodes, but Phase 2.5's host-side checks found that the
onionHEN v0.9.0 host rejects the former and its Settings renderer does
not handle the latter. This plugin now omits `AUTO_START` and renders
informational labels instead of unavailable status/invitation actions.
Regression tests inspect the encoded UI for unsupported disabled flags.

**Since verified (owner, 2026-10-18, 12.20, plugin 1.00):** the original onionHEN v0.9.0
discovered and started this descriptor, and its page showed live daemon data. The custom
onionHEN build of PR #14 is not used ([docs/SHELLUI_HOST_DIAGNOSIS.md](../../docs/SHELLUI_HOST_DIAGNOSIS.md)).
Version 1.10 adds the `NOTIFY` capability (a host-known bit) and is not yet verified on a PS5.

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

Installation instructions are in [docs/shellui/INSTALL-DE.md](../../docs/shellui/INSTALL-DE.md);
every installation stays the console owner's decision. The CI build checks the final ELF
descriptor against the host's known flag mask and the NOTIFY/IPC/UI capability set, as well as the
SDK's own inspector.
