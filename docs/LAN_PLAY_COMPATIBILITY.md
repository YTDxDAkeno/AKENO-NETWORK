# AKENO LAN Play — game compatibility

LAN Play only works for games with a genuine **offline LAN / system-link** mode. A matching title on
two platforms is **not** proof the builds interoperate. This database records what is *documented*
about each title and where that came from; it never asserts a title works over the AKENO tunnel.

Source of truth: `packages/shared/src/lanplay/compatibility.ts`, served at `GET /api/v1/lanplay/games`.

## Honest evidence labels

Used for both a title's own LAN capability (`evidence`) and cross-platform interop (`crossPlatform`):

| Label | Meaning |
| --- | --- |
| `hardware-tested` | Two real devices of this exact build connected and exchanged gameplay over AKENO. **None yet.** |
| `tested-build-pair` | A specific build pair verified (not necessarily over AKENO). |
| `community-reported` | Players report it works; not verified by us. |
| `lan-documented` | Official docs describe a LAN/system-link mode. |
| `not-verified` | Unconfirmed. |
| `incompatible` | Known not to interoperate. |
| `online-only` | Only online/PSN play; cannot work without the service. |
| `requires-bridge` | Would need the companion bridge (all PS5/PS4 cases). |
| `blocked` | Not possible with current, verified capabilities. |

Per-title fields: platform, edition (PS5-native vs PS4-backward-compatible), whether an offline LAN
mode exists, LAN mode type, max players, whether auth is required to reach the mode, cross-platform
evidence, notes, and source URLs with the date verified.

## Current seed entries (all unverified)

| Title | Platform | Offline LAN? | Evidence | Cross-platform | Note |
| --- | --- | --- | --- | --- | --- |
| F1 23 | PS5 (native) | 2-player offline; unofficial LAN | `community-reported` | `not-verified` | Players report PS4/PS5 LAN races; store listings say LAN unsupported. Treat as unverified. |
| Borderlands 2 | PS4 (on PS5 via BC) | No documented system-link | `not-verified` | `incompatible` | Split-screen + PSN online only; no confirmed offline LAN. Research candidate. |
| Borderlands: The Pre-Sequel | PS4 (on PS5 via BC) | No documented system-link | `not-verified` | `incompatible` | Same as Borderlands 2. |

These are **research candidates from the spec**, not endorsements. The spec's other candidates (e.g.
specific Call of Duty titles with documented LAN) should be added only with a source and a verified
date once checked, each against its exact edition and platform.

## Rules for adding a title

1. Cite a source URL and the date verified. No scraped copyrighted assets or artwork — reuse AKENO's
   existing game catalogue artwork where the title is already in it.
2. Default `evidence` and `crossPlatform` to `not-verified` (or weaker). Only a real hardware test
   over the AKENO tunnel earns `hardware-tested`.
3. Record the exact edition and whether the LAN mode needs authentication.
4. Never mark a PS5↔PC pair as crossplay without specific supporting evidence for those exact builds.

## UI behaviour

The web panel surfaces these honestly: a selected game shows its title; nothing implies the game is
playable over the tunnel while the transport is unavailable. The whole feature shows "Transport not
available" above everything until a verified transport exists.
