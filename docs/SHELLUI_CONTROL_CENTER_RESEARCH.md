# Control Center, Game Base and party voice from the ShellUI host — what is possible on 12.20

Status (2026-10-18): **source research of onionHEN v0.9.0 (`aaa4264`) and AKENO's own hardware
results; nothing new run on a PS5.** It answers the owner's three approaches in order and says
exactly what is achieved. German short form: [shellui/DIAGNOSE-DE.md](shellui/DIAGNOSE-DE.md).

> **Phase 4 (2026-10-18):** the proposed host features (1) and (2) of Approach B are now built as
> one experimental patch on top of v0.9.0 — the Toolbox shortcut and a game Options-menu entry open
> AKENO's page directly. See [SHELLUI_DIRECT_ENTRY.md](SHELLUI_DIRECT_ENTRY.md). Not hardware tested.

## What is achieved, and what is not

| Behaviour | Status |
| --- | --- |
| AKENO pages inside PS5 **Settings → OnionHEN Toolbox → AKENO NETWORK** (friends, messages with replies, party, requests, status) | Built and PC-tested (plugin 1.10). The 1.00 page **ran on the owner's PS5** with the original onionHEN |
| Opening them **while a game runs**, from anywhere, with a controller shortcut, without closing the game | Provided by onionHEN itself (Toolbox shortcut, below). **Not yet confirmed on 12.20 with a game running** — hardware test plan rows B1–B6 |
| Returning to the game | Leave Settings (○ / PS button); the game was suspended, not closed. To be confirmed (B5) |
| An AKENO entry **in Sony's Control Center** (the PS-button bar) | **Not achieved.** No supported interface exists; see Approach B |
| AKENO **replacing or living inside Sony's Game Base** card | **Not achieved.** Blocked; see Approach C |
| An **overlay over running gameplay** | **Not achieved.** No host offers one |
| **Party voice during games** | **Not achieved.** Voice works in AKENO Native (foreground); in-game voice is open research (below) |

A Settings page reached by a shortcut is **not** a Control Center integration, and this project
does not call it one.

## Approach A — supported onionHEN extension points (used)

What onionHEN v0.9.0 offers a third-party plugin, from its source:

| Extension point | Source | Usable by AKENO? |
| --- | --- | --- |
| **Plugin pages** in the Toolbox (`onion.ui` ABI 1: pages, menus, labels, actions, lists, toggles, text fields; events back to the plugin) | `libonion_plugin_ui`, `shellui/src/dynamic_ui_*.cpp` | **Yes — this is AKEN00001.** On firmware 0x02300000–0x13ffffff the "legacy-settings" profile enables nested pages and **text input** |
| **Toolbox controller shortcut** `[shortcuts] toolbox=l2_r3 \| long_share \| share` (Toolbox UI: *Controller-Tastenkürzel → OnionHEN-Toolbox öffnen*: "die Toolbox von jedem Bildschirm aus öffnen, auch im Spiel") | `shellui/src/hook_boot.cpp` (`GamePad.GetData` hook, L2+R3 held), `hook_capture.cpp` (Share / long Share) → `GoToURI("pssettings:play?function=debug_settings")` | **Yes, as configuration.** It opens the Toolbox root; the AKENO entry is one press further. ShellUI reads the pad itself, so the `0x809B0081` controller refusal of background payloads does not apply |
| HomeUI **top-navigation icon** → `OnionHEN?NavUI=1` → Toolbox | `shellui/src/homeui_top_nav_patch.cpp` | Yes (the original host shows it on the home screen); not in a game |
| Plugin **notifications** (plain text toasts) | `daemon/source/plugin_host_services.cpp` `notify` → `onion_notify` | **Yes** — plugin 1.10 requests `NOTIFY` and shows each press's result |
| A URI that opens a **specific plugin page** | — (`hook_boot.cpp` knows only `Cheats`, `Cheats_not_open`, `DL_UPDATE`, `NavUI`) | **No.** The shortcut cannot jump straight to AKENO |
| Contributions to Sony menus (Control Center, game Options menu) from a plugin | — | **No** plugin API |

**Result (Milestone B, practical form):** set the Toolbox shortcut to *L2 + R3 gehalten*; in a
game, hold L2+R3 → Settings opens on the Toolbox with the game suspended → *AKENO NETWORK* →
friends, messages, party → leave Settings to return to the game. Two presses, no closed game, no
new hook, no file change. Install guide: [shellui/INSTALL-DE.md](shellui/INSTALL-DE.md#6-schnellzugriff-im-spiel).

## Approach B — controlled runtime ShellUI hooks (researched, not built)

onionHEN already patches Sony UI at runtime in two places that show what a deeper integration
would take:

- **Game Options menu.** `shellui/src/hook_launch.cpp` hooks `ReactNative.Modules.ShellUI.HomeUI
  OptionMenu.createJson` and, with `onionhen_game_opts` on, inserts a "Cheats" entry
  (`MENU_ID_CHEATS`, action URI `OnionHEN?Cheats_not_open`) where Sony builds
  `MENU_ID_CHECK_PATCH`. An "AKENO NETWORK" entry in a game's Options menu would be the same
  pattern plus a new URI (e.g. `OnionHEN?Plugin=AKEN00001`) handled in `hook_boot.cpp`'s URI hook
  and resolved to the plugin's root resource (`dynamic_ui::resource_name`).
- **HomeUI JavaScript bundle.** `homeui_top_nav_patch.cpp` rewrites minified React Native code
  per firmware profile (`homeui_top_nav_profiles.inc`) to add an icon.

**Blocker for doing this in AKENO:** both are changes to the **host** (onionHEN's ShellUI payload),
not plugin features. Doing them means maintaining a modified onionHEN — exactly the PR #14 path
whose layout change turned a silent re-injection into a fatal one
([SHELLUI_HOST_DIAGNOSIS.md](SHELLUI_HOST_DIAGNOSIS.md)) — or injecting a second hook payload into
SceShellUI next to onionHEN's, which the double-detour analysis shows is unsafe for any function
both touch. The Control Center itself is not hooked by onionHEN at all (no reference in its source),
so there is no known-good pattern or firmware-12.20 offset for it; per the project rules no
unverified offsets or speculative binary patches are shipped.

**Proposed path (owner decision):** ask onionHEN upstream for two small, generic host features —
(1) `shortcuts.toolbox_target=<plugin ID>` / a `OnionHEN?Plugin=<ID>` URI that opens a plugin's
root page, and (2) a plugin-facing "game Options menu entry" contribution. With (1) the shortcut
lands on AKENO directly; with (2) AKENO appears in every game's Options menu. Neither needs AKENO to
patch ShellUI itself.

## Approach C — replacing the Game Base entry action (researched, blocked)

Game Base is part of Sony's ShellUI/HomeUI React Native bundle and its PSN-backed social services.
onionHEN has no hook into it, and redirecting its entry would mean rewriting firmware-specific
minified JavaScript (the HomeUI top-nav technique) for a Sony component whose own data comes from
PSN. Blockers: no reviewed patch point, no firmware-12.20 profile, and the predictable fallback the
owner requires (Game Base keeps working when AKENO is off) cannot be guaranteed by a bundle rewrite
that has never run. **Not pursued.** The closest functional alternative is Approach A's shortcut.

## Party voice during games (Milestone C)

| Fact | Evidence |
| --- | --- |
| Two-way party voice works in AKENO Native (foreground app) through the AKENO voice gateway | 1.1.0-beta.1; microphone open/capture/close **verified on 12.20** ([PS5_NATIVE_1.1_HARDWARE_RESULTS_FW12.20.md](PS5_NATIVE_1.1_HARDWARE_RESULTS_FW12.20.md)); server-side voice pass returned 404 there because the gateway was not yet deployed |
| `0x809B0081` is the **controller** refusal (`scePadOpen`) for background payloads, not an audio error | [QUICK_ACCESS.md](QUICK_ACCESS.md), [PS5_NATIVE_ADR.md](PS5_NATIVE_ADR.md) |
| Audio output from a background payload: accepted but not heard (home screen, browser) | audio lab v2, 12.20 |
| Microphone or audible output from a background payload **while a game runs** | **never measured** |
| An onionHEN plugin is a separate background process started through elfldr, like the AKENO daemon | `libonion_plugin_manager`, `daemon/source/plugin_manager_runtime.cpp` |

So the ShellUI host gives voice **no new rights**: AKEN00001 runs under the same rules as the
daemon. A ShellUI-hosted audio path would need code inside SceShellUI — host injection, rejected
above — and onionHEN's other route into running games (Big App SPRX plugins) injects into the
**game** process, which the project rules exclude (no interference with a game's own audio).

**Testable next step (already built):** the Voice Lab (`clients/ps5-voicelab`, PR #10, tests A–E)
measures exactly the missing facts — microphone frames and an audible tone from a background
payload while a game runs. Its decision table in [PS5_BACKGROUND_VOICE.md](PS5_BACKGROUND_VOICE.md)
picks the architecture; nothing is claimed before its report. The companion's party page shows
members' voice state and says voice runs in AKENO Native; the voice controls stay out of the
stable pages, as required.
