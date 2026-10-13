# Firmware compatibility

**Nothing in this project has been tested on a PlayStation 5.** This page tracks what is known,
what is assumed, and what must be demonstrated before any feature is called compatible.

## Status vocabulary

| Status | Meaning |
| --- | --- |
| Compiled | Builds for that target. Says nothing about runtime behavior. |
| Tested in simulation | Runs on Linux against a real AKENO server (`clients/ps5` tests). |
| Tested on hardware | Run manually on a physical console with the listed firmware and components, results recorded below. |
| Stable on hardware | Repeated hardware tests (at least 5 sessions, including restarts and network loss) without a crash. |

## Matrix

| Item | Firmware 12.20 | Firmware 13.60 | Notes |
| --- | --- | --- | --- |
| Jailbreak environment (kstuff) | User-reported | User-reported | Not verified by this project. |
| kstuff version | Unknown | Unknown | Record the exact version and source when testing. |
| ELF/payload loader | Unknown | Unknown | Required to run any homebrew; not chosen or tested yet. |
| etaHEN | Not required | Not required | The client does not use etaHEN or its plugins. |
| Console build of the client | **Not compiled** | **Not compiled** | Needs a console transport (TLS backend) and UI; see below. |
| Client core (JSON, HTTP, WebSocket, pairing, realtime, backoff) | Tested in simulation (Linux) | Tested in simulation (Linux) | Platform-neutral C11; the same code is intended for the console. |
| HTTPS/WSS with certificate verification | Not tested | Not tested | Needs a TLS library built for the target and a CA bundle shipped with the app. |
| Controller input / on-screen UI | Not implemented | Not implemented | |
| Credential storage | Not tested | Not tested | Needs a writable, per-user path; permissions model to be verified. |
| Native Game Base friends, presence, messages, parties, voice, game invites | Unsupported | Unsupported | See [PS5_GAMEBASE_RESEARCH.md](PS5_GAMEBASE_RESEARCH.md). |
| System notifications | Disabled | Disabled | Requires a documented interface verified on that firmware. |

## How compatibility is handled in code

`clients/ps5/include/akeno/caps.h` turns detected **facts** (platform, reported firmware version,
sockets, TLS backend, writable storage) into explicit decisions:

- Network features depend only on generic facts (sockets + verifying TLS + storage), never on a
  firmware number.
- A firmware version is parsed strictly (`MM.mm`). Unknown, unlisted or malformed versions are
  reported as such and **never enable anything**.
- The table of tracked versions (12.20, 13.60) carries `hardware_tested = 0`. It is changed only
  together with a recorded hardware test in this document.
- There are no firmware offsets, patches or version-specific code paths in this project, and none
  may be added without the review described in [SAFETY.md](SAFETY.md).

Run `akeno-cli caps --firmware 12.20` to see the decisions.

## Remaining work for a console build (requires your approval before any hardware test)

1. Choose and document the payload SDK and loader versions, their licenses and their stated
   firmware support (source-level review was **not** done in this session).
2. Add a console transport (`transport.h`) with a verifying TLS library built for the target, and
   ship the CA bundle for the AKENO server.
3. Add a minimal, controller-driven UI (pairing code, friends, messages, invitations, parties,
   sessions) with original AKENO branding.
4. Produce a build artifact and its checksum; do **not** load it automatically.
5. Hardware test plan: a non-primary console, data backed up, network-only features, a clear exit
   path; record results here (firmware, kstuff and loader versions, steps, outcome).

## Hardware test log

_No entries._
