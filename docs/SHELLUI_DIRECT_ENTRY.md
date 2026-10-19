# Direct AKENO entry from a running game (Phase 4, EXPERIMENTAL)

> **HARDWARE RESULT 2026-10-19 — this build FAILED on the owner's PS5 (FW 12.20). Do not retry the
> patched host's direct entry until it is revised.** With `toolbox_target=AKEN00001` the direct root
> rendered (`direct entry: AKEN00001 root page as the Settings root`), then ~13 s later the ShellUI
> plugin bridge disconnected and the console stopped responding to controller input while the game
> kept rendering. Leading suspect: making a **dynamic plugin page the Settings root** and the
> back/focus handoff from it — the one behaviour with no proven onionHEN precedent (onionHEN's own
> Cheats shortcut replaces the root with a *generated Toolbox* page, not a dynamic plugin page). Not
> conclusively a crash; needs on-hardware isolation. **Safe path, now CONFIRMED on the owner's PS5
> (12.20, 2026-10-19): the original onionHEN v0.9.0 + the unchanged plugin, with `toolbox=l2_r3`
> only** — holding L2+R3 in a game opens the normal Toolbox, then Toolbox → Plugins → AKENO NETWORK
> reaches the working social UI, controller input intact. No patched binary. This is the F0 fallback
> and the control case for debugging the patched host. See
> [shellui/phase4/ROLLBACK-DE.md](shellui/phase4/ROLLBACK-DE.md).

Status (2026-10-18): **built and PC-tested, not run on a PS5.** German guides for the owner:
[shellui/phase4/INSTALL-DE.md](shellui/phase4/INSTALL-DE.md),
[shellui/phase4/CONTROL-CENTER-TEST-DE.md](shellui/phase4/CONTROL-CENTER-TEST-DE.md),
[shellui/phase4/ROLLBACK-DE.md](shellui/phase4/ROLLBACK-DE.md).

## Starting point (hardware, owner, FW 12.20)

- AKEN00001 1.10 on the **original** onionHEN v0.9.0 shows the live AKENO pages; messages were sent
  from the PS5 and party details shown.
- With *Atelier Yumia* running, the owner opened AKENO through
  `L3 + Options → DebugMenu: CEX → Launch Settings → OnionHEN Toolbox → … → AKENO NETWORK` and
  returned to the **same running game**. Settings runs next to a suspended game; that is the
  mechanism every route below reuses.

The goal of this phase is to remove the intermediate steps.

## What the unmodified host can and cannot do

From onionHEN v0.9.0's source (`aaa4264`):

| Question | Answer | Source |
| --- | --- | --- |
| Can a plugin navigate, open a page or a URI? | **No.** Its host services are `log`, `notify`, `config_get/set` (own file) and `query_interface` (nothing registered) | `daemon/source/plugin_host_services.cpp`, SDK `services.h` |
| Can a plugin read the controller? | **No** (a background process; `scePadOpen` = `0x809B0081` on 12.20) | [QUICK_ACCESS.md](QUICK_ACCESS.md) |
| Where is AKENO's page linked? | Only on Toolbox → **Plugins** (plugin card → settings link), not on the Toolbox root | `toolbox_xml.cpp` `plugins_model`, `plugin_sprx_pages.cpp` |
| Shortcut that reaches Settings during a game | `[shortcuts] toolbox=l2_r3 \| share \| long_share`: ShellUI's own `GamePad.GetData` / capture hooks → `GoToURI(toolbox route)` | `hook_boot.cpp`, `hook_capture.cpp` |
| URIs the host understands | `OnionHEN?Cheats`, `?Cheats_not_open`, `?DL_UPDATE`, `?NavUI=1` | `hook_boot.cpp` `handle_uri_boot_common` |

So **without a host change** the shortest route is: set the Toolbox shortcut (`toolbox=l2_r3`) →
in the game hold L2+R3 → Toolbox → Plugins → AKENO NETWORK → AKENO NETWORK. It drops DebugMenu
and *Launch Settings* and needs no new binary (fallback F0 in the test plan). It cannot land on
AKENO directly: that needs the host.

## The patch: `tools/onionhen/akeno-direct-entry.patch`

One commit on top of onionHEN v0.9.0. It copies a mechanism onionHEN **already uses during games**:
the Cheats shortcut sends `OnionHEN?Cheats`, the boot-URI hook redirects it to the Toolbox route
and sets a flag, and the resource hook then serves the Cheats page **instead of** the Toolbox root.
Settings opens directly on Cheats; ○ leaves Settings and returns to the game.

| Change | Files | Behaviour with default config |
| --- | --- | --- |
| `OnionHEN?Plugin=<ID>` (9 letters/digits, the plugin manager's ID rule): the boot-URI hook stores the ID and redirects to the Toolbox route; the resource hook serves that plugin's **root page as the Settings root**, once, on a fresh dynamic page stack. A plugin without a renderable page → the Toolbox root, as before, and a log line | `hook_boot.cpp`, `hook_manifest.cpp`, `dynamic_ui_runtime.{hpp,cpp}`, `toolbox_route.hpp`, `shellui_state.hpp` | unused until something sends the URI |
| `[shortcuts] toolbox_target=toolbox \| <plugin ID>`: the **existing** Toolbox shortcut (L2+R3 held 1 s, Share, long Share) sends `OnionHEN?Plugin=<ID>` instead of opening the Toolbox. No new button combination, so no new conflict with games | `hook_boot.cpp`, `hook_capture.cpp`, `libonion_settings` | `toolbox` = upstream |
| `[game_menu] plugin_entry=none \| <plugin ID>`: one entry, titled with the plugin's page title ("AKENO NETWORK"), inserted next to *Check for update* in a game's Options menu (the anchor onionHEN's own Cheats entry uses); its action URI is `OnionHEN?Plugin=<ID>`. Absent while the plugin has no page | `hook_launch.cpp` | `none` = upstream |
| Relocator: an existing `jmp [rip+0]; dq hook` patch at a hooked function (left by an earlier injection) is relocated as one 14-byte unit | `libonion_detour/source/x64_relocator.cpp` | only matters on a repeated injection, see below |

Nothing else changes: no Sony file is written, no new hook is installed, no other process is
touched, kstuff handling, the update deletion, the HomeUI icon and every Toolbox feature stay as
upstream. Config files stay compatible both ways (the original host ignores the two new keys; the
patched host reads an upstream file with the upstream behaviour).

### Why the relocator change (the PR #14 lesson)

A second Toolbox injection into a SceShellUI that already carries onionHEN's detours decodes the old
patch. With v0.9.0's relocator the outcome depends on the bytes of the old hook address, i.e. on the
build layout (`tools/onionhen/check-double-detour.sh`):

| First Toolbox | v0.9.0 relocator | patched relocator |
| --- | --- | --- |
| original v0.9.0 (`+0x318e8`) | 100 % "accepted", but the trampoline does **not** contain the old hook address (the address bytes decode as `call rel32` and are rewritten). Harmless only because `sceRegMgrGetInt_hook` calls the registry syscall, never its trampoline | 100 % accepted, trampoline jumps to the old hook |
| PR #14 host (`+0x2ef48`) | 43 % fail (25 % "relocation failed at +6") | 100 % accepted, trampoline jumps to the old hook |
| this patched host (offset measured in CI) | — | 100 % (CI gate `--expect-chain`) |

Any modified host moves `sceRegMgrGetInt_hook`; with the relocator change that can no longer turn a
repeated injection into a fatal `Detour failed: sceRegMgrGetInt`. It does not make a repeated
injection a good idea: the operating rule stays **one onionHEN per boot, restart to change hosts**.

(Correction to [SHELLUI_HOST_DIAGNOSIS.md](SHELLUI_HOST_DIAGNOSIS.md): "silently chains to the first"
for the original host means *the relocation is accepted*; the produced trampoline would not reach
the old hook. Unused, so the conclusion there is unchanged.)

## Build and evidence (PC)

| Check | Result |
| --- | --- |
| onionHEN's own host test suite on the patched tree (`source/util/tests`, 470 cases; 5 new and 1 extended: URI parsing, root-only takeover, settings round trip and invalid values, root page on a fresh stack and press routing, repeated-detour relocation for 6440×183 addresses) | all pass |
| `check-double-detour.sh` | v0.9.0 facts unchanged; patched relocator chains for every recorded layout |
| Toolchain check: unmodified v0.9.0 rebuilt with this pipeline | `sceRegMgrGetInt_hook` at `+0x318e8` as recorded; `shellui.elf` `.text` the same size as the official release's. **Not byte-identical** to the release: the release was linked by Homebrew clang/LLD 18.1.8 on macOS, this pipeline uses Ubuntu clang/LLD 18.1.3 and ps5-payload-sdk v0.44 with pacbrew-equivalent ports (`build-sdk-ports.sh`) |
| AKEN00001.elf | **unchanged 1.10**, byte-identical to PR #15's CI build (`201d2c88…`) |
| PS5 hardware | **nothing run** |

The patched host is therefore a different binary from the one that ran on the console, built with a
slightly different compiler. That is the residual risk the hardware plan addresses first (H1).

## Control Center and Game Base (Milestone 4.2)

- **Control Center (PS button bar):** onionHEN has no hook, reference or firmware-12.20 profile for
  it. Adding one means a new Mono/React-Native hook without a known-good pattern — no unverified
  offsets are shipped, so it is **not built**. The game's **Options menu** entry is the closest
  native-looking entry point with a known-good pattern; on the PS5 that menu is reachable from the
  game's card (home screen) — whether it is also offered from the Control Center's game card on
  12.20 is test C3.
- **Game Base:** unchanged from [SHELLUI_CONTROL_CENTER_RESEARCH.md](SHELLUI_CONTROL_CENTER_RESEARCH.md)
  (Approach C): blocked, not pursued.
- Milestone 4.2 is reached only when the owner's tests C1–C6 pass.

## Party voice (Milestone 4.3)

Unchanged evidence, re-checked for this phase:

- `0x809B0081` is `scePadOpen`'s refusal for a background payload (facility `0x809B`, not the pad
  library's `0x8092xxxx`); same code on 13.60 from an unrelated payload. It is **not** an audio
  result. The ShellUI shortcut does not depend on it (ShellUI reads the pad itself).
- AKEN00001 is a separate process started by onionHEN's plugin manager through its ELF loader, like
  the daemon. Being an onionHEN plugin gives it **no** audio or microphone rights, and nothing in the
  plugin API exposes ShellUI's audio.
- The open facts (microphone frames and audible output from a background payload while a game runs)
  are exactly what the existing Voice Lab measures. The Phase-4 package therefore ships
  `voice/akeno-voicelab.elf` (unchanged source, separate process, no effect on the social pages) and
  [shellui/phase4/VOICE-TEST-DE.md](shellui/phase4/VOICE-TEST-DE.md). Its decision table
  ([PS5_BACKGROUND_VOICE.md](PS5_BACKGROUND_VOICE.md)) picks between a background voice service,
  talk-only, listen-only, or "foreground only". Code inside SceShellUI (via a host extension) is the
  remaining option if every background variant is silent; it is not started before that report.
