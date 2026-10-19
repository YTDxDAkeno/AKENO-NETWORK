# ShellUI host: why the original onionHEN works and the PR #14 host failed — and the decision

Status (2026-10-18): **source analysis and PC reproduction**, grounded in the owner's hardware
result. Nothing here was run on a PS5 by this work. German summary for the owner:
[shellui/DIAGNOSE-DE.md](shellui/DIAGNOSE-DE.md).

## Decision

**Keep the original, unmodified onionHEN v0.9.0** (`aydencharles/onionHEN` tag `v0.9.0`, commit
`aaa4264588e00da33faccc7642d27c3c4cc339b1`) as the ShellUI host and integrate AKENO only through its
supported plugin interface (`AKEN00001.elf`, onion.ui ABI 1). The custom host of PR #14 is **not
required**: everything AKENO needs — the plugin manager, the plugin bridge, the Settings/Toolbox
route and the dynamic plugin pages — is already in the original host, and the original host is the
one that ran on the owner's PS5 (firmware 12.20) and showed the AKENO NETWORK page. No onionHEN
binary is built, modified or repackaged by this project.

## The evidence

1. **Hardware (owner, 12.20).** The original onionHEN discovered `AKEN00001.elf`, started it and
   showed the page "AKENO NETWORK" with the rows *AKENO*, *Friends online*, *Unread messages*,
   *Quick actions*, *Refresh*, *AKENO In-Game Companion (experimental)*. In plugin 1.00 the rows
   *Friends online*, *Unread messages* and *Quick actions* are only generated when the plugin has
   reached the daemon (`AKN_COMP_READY`) through native interface 2 or later
   (`clients/ps5-ingame/src/ui_doc.c` at `eb81eb4`). So on the console: onionHEN host → plugin
   → AKENO daemon → AKENO server all worked.
2. **Log lines of the PR #14 host** (possibly from more than one start):

   ```
   [plugins] started AKEN00001 1.00 (AKENO NETWORK (experimental))
   [plugin-bridge] ShellUI connected
   Toolbox online for SceShellUI
   DetourFunction: relocation failed at +6
   Detour failed: sceRegMgrGetInt
   toolbox recovery exhausted
   ```

3. **Source, pinned `aaa4264`.**
   - `shellui/src/prx.cpp` `install_hooks()`: the **first, required** hook is `sceRegMgrGetInt`.
     If it fails, `install_hooks` returns false and the ShellUI payload never signals ready.
   - `libonion_detour/source/detour.cpp` `PrepareDetour()` decodes the first `HOOK_LENGTH` = 14
     bytes of the target with onionHEN's relocator and logs `relocation failed at +<offset>` when
     it cannot. `libonion_detour/source/hotpatch.cpp` writes the hook as
     `FF 25 00 00 00 00` (`jmp [rip+0]`, 6 bytes) followed by the 8-byte hook address.
     **Detours are never removed.**
   - `daemon/source/daemon_inject.cpp`: the only guard against injecting the Toolbox into a
     ShellUI that already has it is the ready marker `/system_tmp/onionhen/ready/toolbox` (it
     holds the ShellUI PID). `rest: toolbox recovery exhausted` is printed **only** by the
     rest/NOTE_EXEC re-injection worker after 4 failed attempts.
   - `libonion_conflict/source/conflict.c` refuses to start next to etaHEN, Yoncore, kylin-core,
     wmdw-jwm and CheatRunner — **not next to another onionHEN**. A second bootstrap stops the old
     daemon and util (`launch_pipeline.c` `stop_owned_service`) but cannot remove the old
     ShellUI hooks.

## The mechanism

A pristine `sceRegMgrGetInt` relocates (the original host proves it on this console). "Failed at
**+6**" means the relocator decoded a 6-byte first instruction and then could not decode what
follows — exactly the layout of onionHEN's own patch: `jmp [rip+0]`, then the old hook's address
as data. So the failing injection ran in a SceShellUI **that an earlier onionHEN Toolbox had
already hooked**, after that ShellUI's ready marker was missing or did not match (a partial first
initialisation, a re-injection after rest mode, or a second host in the same boot session).

Whether such a second detour fails depends only on the bytes of the old hook address, i.e. on the
**build layout** of the first `shellui.elf`. Reproduced on a PC with onionHEN's own relocator
(`tools/onionhen/check-double-detour.sh`; offsets from `tools/onionhen/measure-hook-offset.sh`,
Release build, ps5-payload-sdk v0.44, clang 18):

| First Toolbox in ShellUI | `sceRegMgrGetInt_hook` | A second injection over its patch (6440 16-KiB-aligned load bases) |
| --- | --- | --- |
| original v0.9.0 | `+0x318e8` | **100 % accepted** — the address bytes decode as a relocatable `call rel32`. (Phase 4 correction: the trampoline this produces does not contain the old hook address; harmless only because `sceRegMgrGetInt_hook` never calls its trampoline. See [SHELLUI_DIRECT_ENTRY.md](SHELLUI_DIRECT_ENTRY.md#why-the-relocator-change-the-pr-14-lesson)) |
| PR #14 custom host | `+0x2ef48` | **43 % fail**, **25 % exactly "relocation failed at +6"** (decode error), the rest at +8/+9/+10 |

So the same lifecycle situation is **invisible with the original host and fatal with the PR #14
host**. PR #14's patch did not touch the detour code; removing code (HomeUI top-nav, remounts,
update deletion, the cheat HTTP client) moved `sceRegMgrGetInt_hook` to an address whose bytes no
longer decode. The PR #14 host is therefore not a more reliable host — it is the same host with a
different, worse failure mode on re-injection — plus it was cross-built with a different SDK than
the official release and had never run on a console.

(With the original host such a silent second Toolbox is not harmless either — two Toolbox copies
would run inside ShellUI — but it does not break the AKENO page.)

## Operating rules that follow

- **One onionHEN per boot.** Send `OnionHEN.elf` (the original v0.9.0) once after kstuff and the
  ELF loader. Never send it twice, never send a different onionHEN build in the same boot.
- **To change or restart the host: restart the console** (a full restart, not rest mode). Only a
  new SceShellUI process has no old hooks.
- **Never use the PR #14 host.** Delete any copy of `AKENO-ShellUI-Host-EXPERIMENTAL.elf`.
- After a start, `/data/OnionHEN/OnionHEN.log` should show `Toolbox online for SceShellUI pid=…`
  once. `Toolbox already active in SceShellUI` (a second start that skipped injection) or any
  `Detour failed` means: restart the console.
- kstuff: with the owner's kstuff loaded first, onionHEN skips its own
  (`kstuff already running / mprotect OK -- skip launch`) or, with `[kstuff] autoload=false` in
  `/data/OnionHEN/config.ini`, never loads it. Never two kstuff.

## What the original host does to the console (the owner has accepted it by running it)

Unchanged from [IN_GAME_ACCESS.md](IN_GAME_ACCESS.md#what-onionhen-itself-does-to-the-console-not-akeno-but-the-owner-must-know)
and issue #12: it remounts `/system` and `/system_ex`, writes the new file
`/system_ex/vsh_asset/onionhen.png`, deletes a downloaded system update
(`/update/PS5UPDATE.PUP`, `.net.temp`) and unmounts `/update` at every start, and injects its
Toolbox into SceShellUI. These are onionHEN's actions, not AKENO's; AKENO adds none. The AKENO
plugin writes no file and imports no console system function (`console/check-plugin.sh`).

## A second host defect found on the way (affects plugin layout, not the host decision)

`shellui/src/hook_page_stack.cpp`: on every Settings page pop while a dynamic plugin page is
active, onionHEN pops one entry of its dynamic page stack. A plugin **group** (rendered as a nested
`setting_list`) is pushed by the firmware as a page but never pushed on that stack, so leaving a
group pops the *parent* plugin page; afterwards presses on that page are not routed to the plugin
(source reading; consistent with, not yet proven by, the hardware page of plugin 1.00, which used
two groups). Plugin 1.10 therefore uses no groups: only Menu → Page links, which onionHEN pushes
and pops symmetrically. The tests reject any group node.

## What remains unknown (needs the console)

- The exact trigger of the re-injection in the owner's session (the log excerpt is partial).
- Whether descriptions of rows are visible on 12.20 (the screenshot shows none for links and
  labels); plugin 1.10 puts every important fact into titles.
- Text input (the PS5 keyboard on a `text_field`) from a plugin page on 12.20: onionHEN's own
  Toolbox uses `text_field` with `OnPress` delivering the typed value, and its 2.x–13.x profile
  enables plugin inputs, but no AKENO input has been typed on hardware yet.
