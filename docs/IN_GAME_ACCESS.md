# AKENO while a game is running — In-Game Companion (EXPERIMENTAL)

Status (2026-10-18): **plugin 1.00 ran on the owner's PS5 (firmware 12.20) with the original
onionHEN v0.9.0** — onionHEN discovered and started `AKEN00001.elf`, and the AKENO NETWORK page
showed live data from the daemon. **Plugin 1.10** (social pages: friends, messages with replies,
party, requests) is built and tested on a PC (unit tests against onionHEN v0.9.0's page rules, end
to end with the real server and daemon) and **not yet run on a PS5**. The supported host is the
original, unmodified onionHEN v0.9.0; the custom host of PR #14 is not used
([SHELLUI_HOST_DIAGNOSIS.md](SHELLUI_HOST_DIAGNOSIS.md)). Owner guides (German):
[shellui/INSTALL-DE.md](shellui/INSTALL-DE.md), [shellui/TEST-PLAN-DE.md](shellui/TEST-PLAN-DE.md),
[shellui/ROLLBACK-DE.md](shellui/ROLLBACK-DE.md). Control Center, Game Base and in-game voice:
[SHELLUI_CONTROL_CENTER_RESEARCH.md](SHELLUI_CONTROL_CENTER_RESEARCH.md).

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
| **OnionHEN** `b23ffe6` (2026-09-12), aydencharles | The host of the SDK above. The Toolbox is a page inside PS5 **Settings** (which the system opens while a game stays suspended in the background) and can be bound to a controller shortcut (`shortcuts.toolbox`: `l2_r3`, `long_share`, `share`). In-game monitor bar of its own | Offsets include `0x12200000`; Settings route policy names 12.20. **2026-10-18: v0.9.0 (`aaa4264`, the newest commit; `b23ffe6` is older) ran on the owner's 12.20 console with plugin 1.00** | **The supported host**, unmodified |
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

The pages (1.10):

```
AKENO NETWORK
  @you · Online · Nioh 3                > profile: status list (Online, Away, Busy, Invisible)
  Friends · 5 online · 1 playing        > one row per friend > f.<user>: "Send … a message" (PS5 keyboard),
                                          Conversation, Invite to your party / Join their party
  Messages · 2 unread                   > one row per conversation > c.<conversation>: the latest
                                          5 messages, "Reply..." (PS5 keyboard), Mark as read
  Party · Squad · 3/8                   > members with voice state, party chat, Invite friends, Leave
                                          (asks first); or Create a party, Join a friend's party
  Requests & invitations · 2 waiting    > accept / decline friend requests and invitations
                                          (decline asks first), notifications, mark read
  Refresh
  Diagnostics & help                    > daemon, interface, server connection, last problem
```

| Property | Value |
| --- | --- |
| Reads | `/api/native/hello`, `/api/state`, the daemon's local event ring (`/api/events`, every 2 s), and through the daemon's proxy: your profile, friends, conversations, groups, the latest messages of the 6 conversations shown (again only when their newest message changed), current and joinable parties, friend requests, invitations, notifications — the changed sections after an event or a press, everything every 60 s |
| Writes | only routes of the daemon's native write list (IPC 2+): `PUT /presence`, `POST /conversations` (open a direct conversation), `POST /conversations/:id/messages` (with a client UUID, idempotent), `POST /conversations/:id/read`, `POST /parties`, `POST /parties/:id/join`, `/leave`, `/invitations`, `POST /friends/requests/:id/accept`/`decline`, `POST /invitations/:id/accept`/`decline`, `POST /notifications/read`. None are offered when the daemon has no write interface |
| Own server connection | none; no realtime session (the end-to-end test counts them) |
| Console functions | **none**: the ELF imports 57 C-library, socket, thread and clock functions and needs only `libkernel_web`, `libSceLibcInternal` and `libSceNet` (`console/check-plugin.sh` fails the build on anything else) |
| Host capabilities | `NOTIFY` (the result of a press as a system notification), `IPC`, `UI`; flags `LONG_RUNNING`, `STOP_SUPPORTED` (`console/check-host-descriptor.py`) |
| Injection, controller, audio, microphone, browser, game process | none — onionHEN draws the pages and delivers presses and typed text |
| Files | none written; reads `/data/akeno/native.key` (owner-only, checked like AKENO Native does) and `/dev/urandom` (message IDs) |
| Logs | states and verbs only; never names, IDs or message text |
| Licence | AKENO's sources MIT; the ELF links the OnionHEN SDK runtime (GPLv3) and the PS5 SDK runtime, so the **ELF is GPLv3**, shipped only in the experimental package with its source pointer |

### ShellUI page rules (onionHEN v0.9.0 on 12.20)

What the host renders decides the layout; `tests/test_companion.c` checks every rule on the
encoded document:

- **Titles carry the information.** On the console neither link nor label descriptions were
  visible, and the renderer never fills the value column.
- **Only Menu → Page links, no groups.** Leaving a group (a nested list) pops onionHEN's dynamic
  page stack once too often ([SHELLUI_HOST_DIAGNOSIS.md](SHELLUI_HOST_DIAGNOSIS.md)).
- No disabled or secret nodes (the renderer refuses the whole page); every node has a title below
  128 bytes, valid UTF-8, at most 256 nodes and 256 KiB (the largest possible document: 203 nodes,
  49 KB).
- **A control's node ID names its item** (`reply.<conversation>`, `racc.<request>`): onionHEN
  routes a press by node ID against the current document, so a list that changed since the page
  was drawn cannot send a reply into another conversation.
- Text fields: `max_length` 250 — onionHEN checks the typed value in **bytes** against it and
  below 256, the keyboard counts characters.
- A page that disappears (a friend drops off the list) stays for five minutes as a short page that
  says so, so that someone who has it open can go back.
- onionHEN draws a page when it is opened: new data appears when a page is opened again, without
  a ShellUI restart. The plugin keeps its document current all the time.

Voice is not part of the companion: an onionHEN plugin is a background process like the daemon and
gets no audio rights from the host; voice during games stays research
([SHELLUI_CONTROL_CENTER_RESEARCH.md](SHELLUI_CONTROL_CENTER_RESEARCH.md#party-voice-during-games-milestone-c)).
The party page shows members' voice state and points to AKENO Native.

### What is tested where

| Level | What | Where |
| --- | --- | --- |
| Unit (PC, ASan/UBSan) | The model; the encoded document decoded and checked against onionHEN v0.9.0's `validate_document` and legacy-profile render rules and the page rules above (every connection state, read-only daemon, in/out of a party, the largest possible document with multi-byte text at every cut); ID stability across reordered lists; every control routes to its verb; every press against a fake daemon (method, path, body) | `clients/ps5-ingame/tests`, CI host jobs (`ps5-native.yml`, `ps5-shellui.yml`) |
| End to end (PC, real server) | `akeno-ingame-check` reads the pages through the native-IPC daemon from the real server and presses controls the way onionHEN delivers them (`--event <node id> [text]`): status, accept an invitation, reply and mark read, an empty reply, a message from a friend's page, leave / create / invite to a party, accept and decline friend requests, an unknown control; one realtime session | `clients/ps5-native/tests/e2e/run.mjs` |
| onionHEN's double injection | onionHEN v0.9.0's relocator replays a second Toolbox injection over both hook layouts | `tools/onionhen/check-double-detour.sh`, `ps5-shellui.yml` |
| PS5 build | `AKEN00001.elf` built twice (byte-identical), its descriptor checked with the SDK's `inspect_plugin.py` and against the v0.9.0 host's masks, its imports gated; packaged with the native-IPC daemon, German guides and SHA-256 sums | `ps5-shellui.yml`, `ps5-native.yml`, `console/Makefile` |
| onionHEN on a PC | **not possible** — onionHEN and its Toolbox only run on a console |
| PS5 | 1.00: page shown with live data (owner, 12.20). 1.10: **not done** | owner: [shellui/TEST-PLAN-DE.md](shellui/TEST-PLAN-DE.md) |

## Install, test and rollback

The owner has run the original onionHEN v0.9.0 with plugin 1.00 on 12.20. Installing 1.10 is one
file (`/data/OnionHEN/plugins/AKEN00001.elf`) and needs the owner's approval like any change:

- install and start order: [shellui/INSTALL-DE.md](shellui/INSTALL-DE.md) — kstuff → ELF loader
  → AKENO native-IPC daemon → copy the plugin → the **original** `OnionHEN.elf` **once** → start
  the plugin in Toolbox → Plugins;
- hardware test plan (host start, every page and press, in-game shortcut, robustness):
  [shellui/TEST-PLAN-DE.md](shellui/TEST-PLAN-DE.md);
- rollback (back to 1.00, disable, remove, shortcut off, stop using onionHEN):
  [shellui/ROLLBACK-DE.md](shellui/ROLLBACK-DE.md).

### Risks

- onionHEN is third-party GPLv3 software that modifies ShellUI at runtime; its own system changes
  are listed above. A failure can crash ShellUI (the console returns to the home screen or needs a
  restart). One onionHEN per boot; a restart before changing hosts
  ([SHELLUI_HOST_DIAGNOSIS.md](SHELLUI_HOST_DIAGNOSIS.md#operating-rules-that-follow)).
- Plugin 1.10 has not run on a PS5: the keyboard on a plugin page, its notifications, the new pages
  and rest mode are unverified on 12.20.

## In-game access, Control Center, Game Base

The practical in-game path is onionHEN's own Toolbox shortcut (`[shortcuts] toolbox=l2_r3`): hold
L2+R3 in a game → Settings opens on the Toolbox with the game suspended → AKENO NETWORK. That is a
Settings page reached by a shortcut, **not** a Control Center entry, a Game Base integration or an
overlay; why those are not built, and what a host-side path would need:
[SHELLUI_CONTROL_CENTER_RESEARCH.md](SHELLUI_CONTROL_CENTER_RESEARCH.md).
