# Direct PS5 → AKENO SharePlay — plan and blockers

Target: PS5 internal gameplay → AKENO SharePlay → another PS5 or PC, with no capture card and no
PC Remote Play window, using the existing SharePlay transport
([SHAREPLAY_ARCHITECTURE.md](SHAREPLAY_ARCHITECTURE.md)). Evidence:
[PS5_DIRECT_CAPTURE_RESEARCH.md](PS5_DIRECT_CAPTURE_RESEARCH.md).

The backend already has everything except the console-side gameplay stream: parties, invites,
voice, SharePlay authorisation, LiveKit rooms, controller transport, Player 2 grants. This plan
is only about **obtaining the stream on the console.**

## Priority order and current verdict

### A. Native capture API (framebuffer / encoder) — BLOCKED (no interface)
What it would be: a supported call that hands our process the running game's frames (or an
encoded stream). Status: **unsupported** — the pinned SDK has no such interface, `ReadCrc` is a
checksum, `VideoOut` is our own output, and reading another process's buffers is forbidden by
AKENO's rules. **Do not implement.** Only reconsider if a *verified* interface + signature appears
from a credible source; then it gets its own experimental ELF and a Stage-C-style hardware test.

### B. Built-in broadcast pipeline reuse — CANDIDATE (watch-only), needs setup + approval
What it is: the PS5's own hardware-encoded Twitch broadcast, redirected by DNS to an AKENO-run
local receiver that bridges RTMP → a LiveKit track in the party's SharePlay room.

Prerequisites that must ALL hold (verify with the diagnostic + a separate test before building):
1. A **Twitch account is linked** on the console (the broadcaster is the native Twitch feature).
2. The console can **start the broadcast on 12.20** in the user's current configuration.
3. The user sets up a **DNS arrangement that does not disturb the jailbreak DNS `62.210.38.117`**:
   the AKENO receiver must also forward all non-ingest queries to the existing resolver. AKENO
   must never change console DNS itself.
4. The user accepts **multi-second latency** → this path is **watch-only**; it cannot drive
   Player 2 co-op.

If all hold, the build is: a local `RTMP ingest (plain 1935) → H.264/Opus → LiveKit publisher`
receiver, selected in the SharePlay UI as a new source kind (e.g. `ps5-broadcast`), gated behind
explicit user setup and a separate approval. It reuses the existing viewer, authorisation and
party membership unchanged. **Not started**: it depends on the prerequisites, which are currently
unverified on 12.20, and on a separate approval to run any redirection.

Hard "don'ts" for track B: no auto-DNS change; no RTMPS spoofing / cert forging (the main ingest
validates certs); no YouTube blind-redirect (its liveness API check fails); no claim that it is
PSN-independent if starting the broadcast needs an account/session.

### C. Screenshot / recorded-clip — NOT SharePlay
A still or a saved clip is not interactive live video. Not a SharePlay source. (File/symbol
presence is still worth recording via the diagnostic, as evidence.)

### D. Offline activation — IRRELEVANT to capture
See research track D. Does not unlock capture; do not pursue for this goal.

## Decision gate (what the diagnostic results decide)

| Diagnostic result | Next step |
| --- | --- |
| `libSceGameLiveStreaming.sprx` present + status hooks resolve, Twitch linkable on 12.20 | Prototype **B** behind user setup + separate approval (watch-only) |
| None of the broadcast pieces present/usable on 12.20 | Stay on the existing capture-card / Remote Play window source; direct capture remains blocked |
| A *verified* capture/encoder interface surfaces later | Reconsider **A** with its own experimental ELF |

## Performance targets (only if a real capture path is ever unblocked)

Design goals, not console guarantees: **720p30** first, **1080p60** aspirational. Never degrade
game stability for a frame-rate target. Track B inherits the native broadcaster's caps (≤1080p60)
and its latency (seconds), so it meets the watch-only goal but not interactive co-op.

## What stays unchanged

The Alpha 0.5 SharePlay system (browser screen-share and HDMI-capture-card sources, viewer,
Player 2 transport) is untouched and remains the working path. This plan adds a *console-native*
source **only** when the evidence and approvals support it; until then the deliverable is the
read-only diagnostic.
