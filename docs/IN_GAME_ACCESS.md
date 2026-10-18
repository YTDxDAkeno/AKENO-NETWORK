# AKENO while a game is running — In-Game Companion (EXPERIMENTAL)

Status (2026-10-17): **implemented and tested on a PC; built for the PS5; NOT run on a PS5; needs
the owner's explicit approval before anything is installed.** It is a separate experimental package
and not part of the AKENO NETWORK Native release. Nothing here claims that in-game access works on
a console until the owner has completed the hardware proof at the end of this document.

## Why the existing pieces cannot do it

| Approach | Result | Evidence |
| --- | --- | --- |
| Start AKENO Native from the websrv Homebrew Launcher | The launcher starts it as the foreground application; a running game is closed | Native 0.1 hardware test (FW 12.20), `docs/PS5_NATIVE_HARDWARE_RESULTS_FW12.20.md` |
| A background payload that draws or listens to the controller | The daemon could not open the DualSense (`0x809B0081`); background audio is accepted but silent | Alpha 0.5 hardware results (FW 12.20), `docs/PS5_HARDWARE_RESULTS_FW12.20.md` |
| Drawing over a game from AKENO's own process | Would need injection into ShellUI or the game, or a kernel change | Excluded (`docs/SAFETY.md`) |
| Replacing Game Base or the Control Center | Excluded | `docs/PS5_GAMEBASE_RESEARCH.md` |

So AKENO cannot put a menu over a game by itself. A **host** that already owns a supported in-game
surface must show it.

## Hosts reviewed (2026-10-17, source read, nothing installed)

| Project, commit | What it offers a third party | Firmware 12.20 | Verdict |
| --- | --- | --- | --- |
| **OnionHEN plugin SDK** `72527c3` (2026-09-03), OnionBuddies | A documented, versioned C ABI (`onion.ui` ABI 1): a plugin is a **separate ELF process** that contributes pages, groups, labels, menus, lists, toggles, inputs and confirmed actions to the OnionHEN **Toolbox**; selections come back as events (`onion_client_poll_ui_event`); values can be updated (`UI_SET_VALUE`); a contribution is replaced atomically. Host IPC: Unix socket `/system_tmp/onionhen/ipc/plugin_service`; plugins live in `/data/OnionHEN/plugins/<ID>.elf` and are started by OnionHEN | The SDK has no firmware dependency | **Chosen** for the experimental companion |
| **OnionHEN** `b23ffe6` (2026-09-12), aydencharles | The host of the SDK above. The Toolbox is a page inside PS5 **Settings** (which the system opens while a game stays suspended in the background) and can be bound to a controller shortcut (`shortcuts.toolbox`: `l2_r3`, `long_share`, `share`). In-game monitor bar of its own | Offsets include `0x12200000`; Settings route policy names 12.20; **no 12.20 hardware report found** | Required host — owner decision |
| etaHEN `dafa13b` (2026-05-03) | Toolbox and `.plugin` containers; its SDK repository (`LightningMods/etaHEN-SDK`) is **no longer publicly accessible** (2026-10-17); its plugin model includes game `.prx` injection | 12.20 profile listed | Not used: no reviewable SDK, injection model |
| Omega Dashboard `4a7ac76` (2026-10-16) | A replacement dashboard (foreground app) with its own daemon; confirms that background audio is silent | — | Architecture reference only |
| websrv `7748df9` (2026-10-11) | The homebrew launcher; foreground apps only (closes a running game) | Used by the owner | No in-game surface |

### What OnionHEN itself does to the console (not AKENO, but the owner must know)

Read in its README and source; this is what installing OnionHEN would mean, independent of AKENO:

- needs an external ELF loader on port **9021** for its first hop, then runs its own loader on
  **9020**;
- **injects its Toolbox into ShellUI** (`shellui.elf`) — a modification of a system process done
  by OnionHEN, which this project does not do itself;
- **bundles kstuff** but checks first whether one is already running (`kstuff_already_running()`
  in `source/bootstrapper/source/kstuff_probe.c`: an `mprotect` probe) and **skips its own** if
  so; it can also be disabled (`kstuff.autoload=false` in `config.ini`, or `/mnt/usb0/no_kstuff`).
  With the owner's working kstuff loaded first, OnionHEN must therefore not load a second one —
  this has to be confirmed in OnionHEN's log on the console (`kstuff already running … skip
  launch`);
- remounts file systems and, at every start, **deletes a downloaded system update**
  (`unlink("/update/PS5UPDATE.PUP")` and `.net.temp`, then unmounts `/update`;
  `source/bootstrapper/source/bootstrap_filesystem.c`) — the deletion is permanent, the unmount
  lasts until restart; keeps logs and configuration under `/data/OnionHEN/`; optional plugins
  (FTP, DPI) open more ports;
- GPLv3.

These are OnionHEN's own system changes. AKENO's rules (`docs/SAFETY.md`) do not allow AKENO to
make them, so **installing OnionHEN is the owner's decision**, made with the risks below.

## The companion

```
OnionHEN Toolbox (Settings, or the OnionHEN shortcut) while the game keeps running
   │  onion.ui page "AKENO NETWORK"; selections as events
   ▼
AKEN00001.elf  — AKENO In-Game Companion, an OnionHEN plugin process (clients/ps5-ingame)
   │  HTTP on 127.0.0.1:8642 with the daemon's native key (the same interface AKENO Native uses)
   ▼
AKENO daemon, native-IPC build — keeps the only server connection, notifications, game detection
```

The page:

```
AKENO NETWORK
  Friends: 5 online, 1 playing
  Messages: 2 unread in 1 conversation
  Party: Squad, 3 members
  Invitations: 1 party invitation
  Notifications: 1 new
  Status: Online as @you
  This console: Nioh 3
  > Friends online        (a page: who, and what they play)
  > Unread messages       (a page: from whom, the last line)
  Quick actions
    Status   [Online | Away | Busy | Invisible]
    Accept the party invitation   (asks for confirmation)
  Refresh
```

| Property | Value |
| --- | --- |
| Reads | `/api/native/hello`, `/api/state`, friends, conversations, groups, current party, friend requests, invitations, notifications, your profile — all through the daemon, every 10 s and after each action |
| Writes | **two**, both on the daemon's IPC 2 write list: `PUT /presence` and `POST /invitations/:id/accept` (confirmed); disabled when the daemon has no write interface |
| Own server connection | none; no realtime session (the end-to-end test counts them) |
| Console functions | **none**: the ELF imports 57 C-library, socket, thread and clock functions and needs only `libkernel_web`, `libSceLibcInternal` and `libSceNet` (`console/check-plugin.sh` fails the build on anything else) |
| Injection, controller, audio, microphone, browser, game process | none — OnionHEN draws the page and delivers the selections |
| Files | none written; reads `/data/akeno/native.key` (owner-only, checked like AKENO Native does) |
| Licence | AKENO's sources MIT; the ELF links the OnionHEN SDK runtime (GPLv3) and the PS5 SDK runtime, so the **ELF is GPLv3**, shipped only in the experimental package with its source pointer |

Voice is not part of the companion: background audio is silent on 12.20 (above), so voice during
games stays research (`docs/VOICE.md`, V5).

### What is tested where

| Level | What | Where |
| --- | --- | --- |
| Unit (PC, ASan/UBSan) | The menu model, every connection state, long texts, and the OnionHEN page validated by the SDK's own `onion_ui_document_validate` | `clients/ps5-ingame/tests`, CI host job |
| End to end (PC, real server) | `akeno-ingame-check` reads the menu through the native-IPC daemon from the real server (unread messages, a live party invitation, status), changes the status to Away (bob sees it) and accepts the party invitation (the server shows 2 members); one realtime session | `clients/ps5-native/tests/e2e/run.mjs` |
| PS5 build | `AKEN00001.elf` built twice (byte-identical), its `.onion_plugin` descriptor checked with the SDK's `inspect_plugin.py`, its imports gated | CI PS5 job, `console/Makefile` |
| OnionHEN on a PC | **not possible** — OnionHEN and its Toolbox only run on a console |
| PS5 | **not done** | owner, below |

## What the owner has to decide

1. **Whether to install OnionHEN** (`b23ffe6` or a newer release you trust) next to the current
   setup. It is a whole HEN with the system changes listed above. AKENO does not install it.
2. Whether the order **kstuff (yours) → OnionHEN** is acceptable, so that OnionHEN skips its own
   kstuff; or to set `kstuff.autoload=false` in `/data/OnionHEN/config.ini` before the first start.
   **Never run two kstuff.**
3. Whether to try the companion plugin afterwards.

### Risks

- OnionHEN is third-party GPLv3 software that modifies ShellUI at runtime. A failure can crash
  ShellUI (the console returns to the home screen or needs a restart). It has **no confirmed 12.20
  hardware report** that we found.
- Rest mode: OnionHEN restores its Toolbox after rest mode (kstuff-lite's method); the companion
  reconnects by itself. Untested on your console.
- The companion is new code that has never run on a PS5.

### Backup before the first try

1. Note the exact steps you use today (exploit host, kstuff, elfldr port 9021, websrv, AKENO
   daemon) — they do not change.
2. Copy `/data/akeno/` (pairing, settings) to a PC with FTP or a USB stick.
3. Make sure the current AKENO release and fallback files are on the PC
   (`akeno-network-native-1.0.0-beta.1/fallback/`).
4. Do not change any game, account or system setting.

### Install (only after approval)

1. Start the console as usual: exploit, **your kstuff**, ELF loader on 9021, the AKENO native-IPC
   daemon. Check AKENO Native works (Friends load).
2. Send `OnionHEN.elf` with your ELF loader. Wait for its notifications; open **Settings →
   OnionHEN Toolbox**. In `/data/OnionHEN/OnionHEN.log` look for
   `kstuff already running / mprotect OK -- skip launch`. If instead it loaded kstuff, stop and
   report it (rest mode / restart clears it).
3. Copy `experimental/in-game-companion/AKEN00001.elf` (from the release package) to `/data/OnionHEN/plugins/AKEN00001.elf` (FTP or USB).
4. Let OnionHEN reconcile its plugins (Toolbox → Plugins, or restart OnionHEN). An **AKENO
   NETWORK** page appears in the Toolbox.

### Rollback

- Remove the companion: delete `/data/OnionHEN/plugins/AKEN00001.elf` (or remove it in the Toolbox
  plugin manager). Nothing else of AKENO's is changed.
- Remove OnionHEN: restart the console and do not send `OnionHEN.elf` again (it runs only after
  you send it; its ShellUI changes and mounts end with the restart); optionally delete
  `/data/OnionHEN/`. A system update file it deleted while it ran stays deleted (the console
  downloads it again if updates are allowed).
- AKENO Native and the daemon do not depend on OnionHEN; nothing to roll back there.

## Hardware proof (required before anyone may call it working)

Use a low-risk single-player game. Record each line (photo or log):

| # | Check | Result |
| --- | --- | --- |
| 1 | The game is running | |
| 2 | The AKENO page opens through OnionHEN (Settings → Toolbox, or the configured shortcut) | |
| 3 | The game is still running (suspended, not closed) | |
| 4 | The page shows real AKENO data (friend count, unread count match AKENO Native / the website) | |
| 5 | The page responds to the controller (open "Friends online", change Status, Refresh) | |
| 6 | Closing Settings returns to the game, which resumes | |
| 7 | Game audio and controls work normally afterwards | |
| 8 | The AKENO daemon still works (AKENO Native or the page shows live data afterwards) | |
| 9 | No crash, no error dialog, normal behaviour after a restart | |

Also report: OnionHEN's log line about kstuff, `/data/OnionHEN/OnionHEN.log` around the plugin
start (`[akeno-ingame] connected to OnionHEN`), firmware, and the daemon's `akeno.log`.

## Later, if the proof passes

- More actions through the same write list (accept/decline friend requests, decline invitations).
- A shortcut-first flow (`shortcuts.toolbox`) documented for quick access.
- Voice during games stays separate research (V5): it needs evidence that a background process can
  play audio, which today it cannot.
