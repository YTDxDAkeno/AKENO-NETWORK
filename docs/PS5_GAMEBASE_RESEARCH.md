# Native Game Base integration — findings

**Question:** can the PS5's own Game Base (friends list, presence, messages, parties, voice,
game invitations) show and use AKENO NETWORK instead of, or next to, PlayStation Network?

**Answer for this project: no.** No documented interface was identified that lets a homebrew
application supply friends, presence, messages, parties or invitations to the system UI. Every
route to that result would need one or more of: modified system software, hooks into system
processes, kernel changes, or impersonation of Sony services. These are excluded by this
project's safety rules ([SAFETY.md](SAFETY.md)) and by the project brief itself. Native Game Base
integration is therefore recorded as **unsupported** — not "planned", and not replaced silently by
something else.

## Evidence levels

1 implemented and tested · 2 in source but not independently tested · 3 documented but
unverified · 4 plausible but unproven · 5 not realistically available through identified
interfaces.

| Capability | Finding | Level |
| --- | --- | --- |
| Read or change the system friends list from homebrew | No public or documented interface identified. Friends are PSN account relationships served by Sony. | 5 |
| Make Game Base consume an alternative friends backend | Would require replacing the data source of a system UI component (system-software modification or service impersonation). | 5 |
| Show AKENO presence in system UI | Same dependency on PSN identity and system UI internals. | 5 |
| Native messages with AKENO accounts | System conversations are tied to PSN account IDs; no interface for a foreign identity provider identified. | 5 |
| Native party creation, membership, voice | Party sessions, microphone routing and voice transport are system services bound to PSN session identity; detaching them would need the excluded techniques. | 5 |
| Join proprietary games through an AKENO lobby ID | Games use their own and the platform's session services; an independent lobby ID means nothing to them. | 5 |
| Invitations to a game or homebrew written for AKENO | Possible for software that calls AKENO's API itself (the device client does exactly this). | 1 (API, Linux) |
| Independent homebrew client with AKENO friends, chat, parties | The client logic exists and is tested on Linux; a console build is not done. | 1 (Linux) / not tested on console |
| System notifications (toasts) from homebrew | Not investigated to a verified interface; disabled in the capability policy until a documented, firmware-tested interface exists. | 4 |

Notes:

- "Level 5" here means *not available through any interface this project may use*. It is not a
  claim that nobody could ever modify a console to do it.
- A working network connection, a payload loader or a replacement dashboard is **not** evidence of
  Game Base integration. Omega Dashboard (GPL-3.0-or-later) shows that an independent homebrew
  client with its own server is feasible; it does not show that the system friends or party UI
  can use a different backend (evidence level 2 for the client architecture, not tested here).

## What was verified in this session, and what was not

| Item | Status |
| --- | --- |
| AKENO server, device pairing, scoped tokens, realtime for devices | Implemented and tested (server tests, browser E2E) |
| Console client logic (C11) against the real server | Implemented and tested on Linux, including TLS verification, restarts and revocation |
| Source-level review of kstuff-lite, etaHEN, etaHEN-SDK, ps5-payload-dev/sdk | **Not performed in this session.** No claims about their APIs, loaders or firmware support are made here. |
| Any test on a PS5 (12.20 or 13.60) | **None.** Nothing was sent to, installed on or run on a console. |

## The practical path

The closest safe alternative is the **independent AKENO client** (Level 2 in
[PS5_FEASIBILITY.md](PS5_FEASIBILITY.md)): a separate homebrew application with original AKENO
branding that uses the paired-device API for friends, presence, messages, invitations, parties
and game sessions. Its core is in `clients/ps5`; what is still missing for a console build is
listed in [FIRMWARE_COMPATIBILITY.md](FIRMWARE_COMPATIBILITY.md) and
[PROJECT_STATUS.md](PROJECT_STATUS.md).

Players still meet inside games through each game's own systems; AKENO game sessions carry an
optional lobby code or similar connection hint for participants.
