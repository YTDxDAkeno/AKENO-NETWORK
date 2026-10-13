# PlayStation 5 feasibility

**Summary:** AKENO NETWORK is a website plus an independent device client. The server runs on a
PC or Linux server and requires no PlayStation, no PSN account and no console modification.
**Nothing was installed on, sent to, or tested against a physical PS5** during this project. It
does not replace or integrate with PlayStation Network features.

_Update 2026-10-13 (session 3):_ the Level 2 console app (Alpha 0.1) exists: a payload with
verified TLS, code pairing, a page in the system browser and system notifications, built in CI
and tested on Linux; it has not yet run on a PS5. See [PS5_CLIENT.md](PS5_CLIENT.md).

_Update 2026-10-13:_ Level 2 (independent client) has started: device pairing is implemented on
the server, and the client logic (`clients/ps5`) is implemented and tested **on Linux**. Native
Game Base integration (Levels 3–4) remains unsupported; findings are in
[PS5_GAMEBASE_RESEARCH.md](PS5_GAMEBASE_RESEARCH.md) and
[PSN_COMPATIBILITY_RESEARCH.md](PSN_COMPATIBILITY_RESEARCH.md). Safety rules are in
[SAFETY.md](SAFETY.md).

This document explains four very different meanings of "running on PS5", what each would
require, and what this project will and will not do.

## Safety policy for this project

Because an earlier experimental mod loader crashed a console and required a system reset,
V1 follows these rules:

- No kernel payloads, exploits, injectors, process hooks, firmware or system-file changes.
- No redirection of Sony authentication or service domains, and no impersonation of PSN.
- No assumption that DNS changes provide PSN compatibility (they do not; see
  [VPS_DEPLOYMENT.md](VPS_DEPLOYMENT.md#what-dns-does--and-does-not-do)).
- No installation of anything on a console and no experimental commands sent to a console.
- All development and testing happen on PCs, browsers and Linux servers.

## The four integration levels

### Level 1 — Standard web application (this project)

| | |
| --- | --- |
| What it is | AKENO NETWORK opened in a web browser. |
| Prerequisites | A browser and network access to your AKENO server. |
| Console risk | None from AKENO's side; it is an ordinary web page. |
| Status | Implemented for desktop and mobile browsers (tested in Chromium, including a 390 px mobile viewport). |
| PS5 browser | **Untested.** The PS5 does not list a general-purpose web browser in its main menu; browser access is only reported to be reachable indirectly, and its capabilities for this kind of app are undocumented. Whether it supports this app's JavaScript, WebSockets, cookies and layout is unknown (not verified by this project). If it is ever tested, limitations must be reported rather than worked around by changing console behavior. |
| Unavailable here | Microphone access (`getUserMedia`/WebRTC) in a console browser must not be assumed. |

The web client includes optional, experimental **controller navigation** using the standard
browser Gamepad API (D-pad moves focus, A selects, B goes back). This only reads controller
input inside the browser tab; it is opt-in under Settings → Preferences.

### Level 2 — Independent native homebrew client

| | |
| --- | --- |
| What it is | A separate application running on the console that talks to AKENO's API (like the Omega Dashboard client talks to its own server). |
| Prerequisites | A console in a homebrew-capable state (which generally involves exploiting the system), a toolchain, and a client that implements AKENO's API — for example via the device-authorization flow in [GAME_INTEGRATION.md](GAME_INTEGRATION.md). |
| Console risk | Significant: depends on unofficial system access; can crash or destabilize the console, and may violate platform terms. |
| Status | **In progress.** Device pairing and scoped device tokens are implemented and tested on the server ([DEVICE_PAIRING.md](DEVICE_PAIRING.md)). The client core in `clients/ps5` is implemented and tested on Linux against the real server. No console build exists and nothing has run on a console; a console build and any hardware test need your explicit approval ([FIRMWARE_COMPATIBILITY.md](FIRMWARE_COMPATIBILITY.md)). Omega's source shows such a client is technically possible (evidence level 2). |

### Level 3 — Alternative homebrew dashboard

| | |
| --- | --- |
| What it is | Replacing or overlaying parts of the console's own UI with a custom dashboard that includes friends/chat. |
| Prerequisites | Everything from level 2, plus deep integration with system processes and UI. |
| Console risk | High: system-level modification is exactly the class of change that previously caused a crash. |
| Status | **Rejected** for this project. |

### Level 4 — Native replacement for system-level PSN functions

| | |
| --- | --- |
| What it is | Making the console's own friends list, parties, voice chat, Share Play, game presence or game multiplayer use AKENO instead of PSN. |
| Prerequisites | Reimplementing proprietary, undocumented Sony network protocols per feature and per game, intercepting or redirecting console traffic, and modified system software. |
| Console risk | Highest; also legal and account risks. |
| Status | **Not realistically available** (evidence level 5 for this project). Projects such as OrbisNet (closed source, jailbroken PS4, game-specific) and BlueSphere (documented auth/presence endpoints, no verified self-hostable backend) show how narrow and game-specific such work is; neither is used. |

## What AKENO NETWORK provides instead

- Its own accounts, friends, presence, chat, groups and text parties, usable from any modern
  browser on a PC, phone or tablet.
- A game-session directory to coordinate who plays what and when, with connection details
  (for example an in-game lobby code) shared only with participants. Players then meet
  **inside the game using the game's own systems**.
- Self-reported "what I'm playing" activity, clearly labeled as self-reported.

## Recommendation

Stay at level 1. If PS5 browser access is ever evaluated, do it as a read-only compatibility
check (does the page load, can you sign in and chat?) and document results honestly. Any
native work (level 2) should start only after V1 is stable, with an explicit authorization
decision, a non-primary console, and no changes to system software.
