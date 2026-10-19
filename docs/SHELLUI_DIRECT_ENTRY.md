# Direct AKENO entry from a running game (Phase 4 and 4.1, EXPERIMENTAL)

> **Phase 4 (below) FAILED on the owner's PS5 (FW 12.20, 2026-10-19); do not use
> `OnionHEN-AKENO-direct.elf` again.** Phase 4.1 ([next section](#phase-41)) replaces it with
> `OnionHEN-AKENO-direct-fixed.elf`: by default the shortcut opens the Toolbox with AKENO first and
> selected (no Settings-root takeover); the direct root is opt-in, corrected and instrumented.
> **Hardware (owner, FW 12.20, 2026-10-19, host `560a2202…`): H1–H5 passed** – the new host with
> upstream behaviour, and link mode on the home screen and in a running game (*Atelier Yumia*),
> returning to the same game with the controller responsive. **Root mode: opens AKENO directly in a
> running game, repeated open/close cycles included, game and controller keep working; the Phase 4
> freeze did not reproduce.** The owner's log of that boot shows 10 direct entries, each with
> `root page served`, `root activated` and `session ended (root left, Settings closing)`, pad reads
> continuing in the input watch, and none of the stop lines (no ShellUI disconnect, no second
> injection, no `recovery exhausted`, no `root mode off`). Confirmed on this console (FW 12.20); not
> labelled stable (no prolonged test), and nothing is known for other firmware. The original onionHEN v0.9.0 + `toolbox=l2_r3` path (F0,
> passed 2026-10-19) stays the fallback. German guides:
> [shellui/phase4-fix/INSTALL-DE.md](shellui/phase4-fix/INSTALL-DE.md),
> [TEST-DE.md](shellui/phase4-fix/TEST-DE.md), [ROLLBACK-DE.md](shellui/phase4-fix/ROLLBACK-DE.md).

## Phase 4.1

### What failed

The owner ran the Phase 4 host with `toolbox=l2_r3`, `toolbox_target=AKEN00001` and
`plugin_entry=AKEN00001`. The game kept rendering, AKENO did not stay usable and the console stopped
responding to the controller until a full restart; the original host afterwards worked normally (F0).
The relevant boot:

```text
[ 93.180] Toolbox online for SceShellUI pid=61 (ready protocol)
[135.418] Big App started ... tid=PPSA25468
[136.756] rest: toolbox recovery exhausted gen=1 requested_pid=101
[170.927] [ShellUI] info: direct entry: AKEN00001 root page as the Settings root
[183.804] [plugin-bridge] ShellUI disconnected
[197.131] Activating toolbox...
```

### Evidence, by strength

**Confirmed from the source (v0.9.0 + the Phase 4 patch):**

1. **The input path did not change.** In `GetData_hook` (L2 + R3) and the capture hooks (Share, long
   Share) the patch only swapped the URI string. The pad read and `result.Buttons = None` on the one
   activation frame are upstream's; the only state is the per-hold timer, reset on release. The
   shortcut code cannot keep input captured. Consistent with F0 passing on the original host.
2. **The direct root broke onionHEN's Settings-root convention.** Every page onionHEN serves for
   `debug_settings.xml` has the root `setting_list` id `id_debug_settings`: the Toolbox root
   (`generate_toolbox_xml`) and the page of onionHEN's own in-game shortcut `OnionHEN?Cheats`
   (`generate_cheats_xml` switches its list id to `id_debug_settings` exactly when it runs as the
   Settings root). Phase 4 served the plugin page with its child-page id `id_onion_ui_…`; its own PC
   test even required the direct root to equal the linked child page byte for byte. This is the only
   structural difference between the proven Cheats-as-root mechanism and the failed takeover (same
   boot redirect, same resource hook, same `plugin="debug_settings_plugin"`, no root focus in either).
3. **Page ownership was wrong for a root.** `set_active_page(DynamicPlugin)` treats the page as a
   child and records whatever page was active before (possibly from an earlier Settings session) as
   its parent, and a closed Settings left plugin pages on the dynamic stack. Wrong state, not shown to
   crash anything, fixed anyway.
4. **PR #16's relocator change read out of bounds:** a 6-byte `memcmp` at every instruction offset
   (ASan, `test_x64_relocator`). On the console it reads a few bytes of mapped code during
   injection, which succeeded at 93 s; not a plausible cause of the freeze, fixed.

**Inferred from the log and the daemon source (strong, not proven):**

5. **SceShellUI was most likely replaced around 183.8 s.** `[plugin-bridge] ShellUI disconnected` is
   the daemon's read loop ending. If only the socket had dropped while ShellUI lived, its bridge
   thread would reconnect within about a second (`plugin_ui_bridge_client.cpp`: `sleep(1)`, connect);
   no reconnect is in the excerpt. Outside rest mode, `Activating toolbox...` is logged by the
   SceSysCore NOTE_EXEC worker for a *newly executed* SceShellUI, after it waits for the trophy SPRX
   and the default 10 s `resume_reinject_delay_seconds`: 183.8 s + ~3 s + 10 s ≈ 197.1 s. A crash
   and a watchdog kill of a hung UI thread both fit; the log cannot tell them apart (no crash dump, and
   the `[lifecycle] SysCore EXEC SceShellUI pid=…` line is debug-level).

**Hypothesis (the most likely mechanism, needs the console):** Sony's legacy Settings addresses its
root page as `id_debug_settings`; a root without that list leaves the DebugSettingsOld screen with no
usable root page/focus (the 13 s fit a UI-thread hang ended by a watchdog better than an immediate
crash), SceShellUI is replaced while a game is in the foreground, and input focus does not return to
the game. Sony's Legacy.dll is not available here to confirm it. *Update 2026-10-19:* the 4.1 root
mode opens AKENO directly on the owner's console in a running game, repeatedly, and the log shows
every session ending through `SettingPageStack.OnPopping` on the root (so the session file and the
next-Settings-root fallback were not needed). That is consistent with this hypothesis but does not
isolate the root list id: 4.1's root mode also changed the root page's ownership and added the
session guards.

**Not causal / open:**

- Controller hardware and the game: ruled out by the A/B (original host fine before and after).
- Repeated injection: no `Detour failed`/`relocation failed` in that boot; the re-injection at 197 s
  went into a new process.
- `rest: toolbox recovery exhausted gen=1 requested_pid=101` (1.3 s after the Big App start): the
  daemon's NOTE_EXEC worker for a process it identified as SceShellUI (pid 101, not the live 61)
  failed to inject. Daemon code neither patch touches; it was reported in working logs too. Side
  effect worth knowing: `toolbox_on_new_shellui` clears the Toolbox ready marker while pid 61 still
  carries the Toolbox. Not shown to matter here; left as is.
- Toolchain: the patched host is built with Ubuntu clang 18.1.3, the official release with Homebrew
  clang 18.1.8. All ShellUI hooks are installed by Mono class/method name, not fixed offsets; the
  one native detour is checked by `check-double-detour.sh` at the measured offset. Tests H1–H4 run
  the new binary with upstream behaviour first to separate a build problem from the direct entry.

### What Phase 4.1 changes (`tools/onionhen/akeno-direct-entry-fix.patch`, applied after Phase 4's)

| Change | Why |
| --- | --- |
| `[shortcuts] toolbox_target_mode=link\|root`, **default `link`**: the shortcut opens the **unchanged Toolbox route**; the Toolbox root lists the `toolbox_target` plugin's page first with the initial focus (the very link the Plugins page has). ✕ opens AKENO as a child page, ○ returns to the Toolbox, ○ to the game | Uses only mechanisms proven on the owner's console (Toolbox root, `root_focus`, a plugin page as a child). PR #16's config (`toolbox_target=AKEN00001`, no mode) now means link mode: the configuration that failed no longer replaces the Settings root |
| Root mode (opt-in): the plugin's root page with root list `id_debug_settings`; `ToolboxUiState::enter_settings_root` (no parent, no Cheats flag) | Same contract as the Toolbox root and `OnionHEN?Cheats` |
| `direct_entry.hpp` session: a request is taken once, expires after 15 s, is cancelled by any other OnionHEN route; repeated shortcuts within 2 s are one request; a plugin without a page opens the Toolbox; any other Settings root drops plugin pages a closed Settings left open | Duplicate, stale, reentrant and interrupted requests |
| Session file `/system_tmp/onionhen/akeno_direct_session` names the SceShellUI holding a session; a SceShellUI that starts while one is open after activity in the last 120 s logs it and turns root mode off until reboot (`akeno_direct_disabled`) | Tells a ShellUI replacement apart from a bridge disconnect; no repeat after a crash/restart loop |
| `AKENO_DIRECT:` log lines for each step and a 60 s input watch (ShellUI pad reads and button changes, one line per 5 s) | Crash (next ShellUI reports the open session), hang (watch lines stop), lost focus with ShellUI alive (watch lines continue while the owner sees no reaction), page rejected (`root page served` without `root activated`). No control ids, values or message text are logged |
| Relocator: byte-wise `jmp [rip+0]` test | The ASan finding above |

Not changed: the plugin (byte-identical `201d2c88…`), daemon, server, kstuff handling, every
other Toolbox feature, `GetData_hook`'s button handling. `[game_menu] plugin_entry` keeps its Phase 4
code (it sends the same URI, so it follows the mode) but stays `none` in this test round.

### Verification (PC only)

- onionHEN's host suite on the patched tree: **483/483** (470 existing + 13 new: `direct_entry.*`
  ×12 – shortcut route, duplicates, single take, expiry/cancellation, session file and input watch,
  replacement, restart during a session, restart long after, record format, root ownership, config
  compatibility, concurrent hooks – and `plugin_ui_direct_entry_flow`, which drives the hook steps
  against the real registry, renderer, page stack and `ToolboxUiState` for 25 open/close rounds,
  closing without a pop, the F0 route, a daemon disconnect, a stopped plugin and a ShellUI restart).
- The same suite under ASan/UBSan and TSan (`check-host-tests.sh`): 483/483 each; no sanitizer report
  on a patched line. Four reports on unchanged upstream lines remain (three misaligned loads in
  `hde64.cpp`, one use-after-scope in upstream's `test_toolbox_route.cpp:165`). The gate fails on
  Phase 4's patch alone (the relocator over-read), so it does catch patched-line defects.
- `check-direct-entry.sh`: all 12 real AKEN00001 documents (up to 203 nodes) open from the Toolbox
  root link and as the Settings root with `id_debug_settings`, reach the same pages, route every
  press (ASan/UBSan).
- `check-double-detour.sh`: chains for every recorded layout and for this host's `+0x33618`.
- Host built twice with the pinned SDK: byte-identical. Plugin: byte-identical to `201d2c88…`.
- **None of this shows that the freeze is fixed.** Only the owner's console can (TEST-DE.md H1–H10).

---

## Phase 4 (failed on hardware; kept for reference)

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
