# SharePlay status (Alpha 0.5, experimental)

_Last updated: 2026-10-16. Source of truth for resuming SharePlay work. Nothing here is marked
done unless it was actually run; "hardware" means a real PS5, controller or second home._

- Branch: `feature/akeno-party-shareplay` (mirror of the session branch `claude/youthful-cori-gp8duz`)
- Base: `claude/vibrant-mendel-v55zqf` @ `ae948512475a6752fb4cebd70234893e037044e1` (Alpha 0.4)
- Docs: [architecture](SHAREPLAY_ARCHITECTURE.md) · [PS5 research](SHAREPLAY_PS5_RESEARCH.md) ·
  [hardware test plan](SHAREPLAY_HARDWARE_TEST.md) · [integration](SHAREPLAY_INTEGRATION.md)
- Production: **not deployed, not touched** (no SSH, no images, no migrations, no secrets).

## Summary

| Area | Status |
| --- | --- |
| SharePlay sessions, role-scoped LiveKit tokens (separate room from voice), webhooks, reconciliation | Implemented · automated tests |
| Browser streaming: window/screen (e.g. PS Remote Play window) and HDMI capture card, H.264 + simulcast, Opus stereo game audio | Implemented · browser E2E with real LiveKit and **generated** video |
| Viewer in the party page: video, audio, volume, mute, fullscreen, stats, reconnection, ended state, cleanup; stream across pages | Implemented · browser E2E |
| Host controls: capture indicator, viewer count, quality, stop; privacy warning before the browser picker | Implemented · browser E2E |
| Player 2 permission flow (request / allow / deny / revoke / give back / automatic revocation) | Implemented · server tests + browser E2E |
| Gamepad API sender (normalization, dead zones, PS/Create blocked, neutral on blur/hidden/disconnect/stall) | Implemented · unit tests + E2E with a **scripted** gamepad; no real controller tested |
| Controller transport (32-byte protocol, grant/epoch/sequence/rate limits, watchdog 500 ms) | Implemented · automated tests |
| Host browser input monitor | Implemented · E2E |
| PS5 console app diagnostic receiver (SharePlay tab, opt-in, display only) | Implemented · C unit + integration (Linux host build); PS5 ELF compiles + import gate OK; **never run on a PS5** |
| Native PS5 game capture | **UNSUPPORTED** — no verified mechanism (research §2) |
| Virtual second controller on the PS5 / real Player 2 in a game | **UNSUPPORTED in AKENO** — undocumented signatures; known methods need kernel credential changes and SceShellCore injection (research §3). Stage D blocked. |
| LAN tunnelling | Feasibility report only; no prototype (no verified compatible title) |
| Bandwidth/latency on real networks | Not measured (container SFU cost measured; estimates documented) |

## Tests executed (2026-10-16, Linux container, Node 22.22, PostgreSQL 16, Chromium, LiveKit 1.12.0 binary)

| Check | Result |
| --- | --- |
| `pnpm lint`, `pnpm typecheck` | pass |
| `pnpm test` | shared **20** (12 new), server **188** (21 new: `shareplay.test.ts`), web **39** (5 new) |
| Browser E2E with real LiveKit | **8/8** in a full run incl. `shareplay.spec.ts`; the SharePlay spec was then hardened against ramp-up timing (2 of 3 repeated runs had passed before the fix, then passes) |
| C client + console app (ASan/UBSan, mbedTLS + OpenSSL) | 564 checks per build, 0 failures; Valgrind clean (release) |
| C integration (`run.mjs` with `--app --audiolab`) | all steps pass incl. "receives SharePlay Player 2 input as a diagnostic receiver" |
| PS5 ELF with the pinned SDK `f5d40f0` (local) | builds with `-Werror`; `check-imports.sh` OK, **96 imports, same system functions as Alpha 0.4** |
| GitHub Actions | CI and PS5 build green on `65bd581`, `8eabbe6`, `c870a87`; see the PR for later runs |
| Resource measurement (`shareplay-measure.mjs`) | LiveKit ~5–6 % of one core, ~107 MB, 1–3 viewers; client bitrates not representative (software encoder starved) |

Generated video and the scripted gamepad are test instruments only; they are never shipped as a
capture source or controller.

## Known limitations

- A server restart ends every SharePlay session and grant (in-memory state, by design).
- One broadcaster per party; one Player 2; default 4 viewers.
- Streaming needs a desktop Chromium-class browser; Firefox/Safari/real Android untested.
- Viewers on another AKENO page keep the game audio; the video subscription pauses until the
  party page is open again (bandwidth saving).
- Capture cards that expose audio under an unrelated name stream video only (the UI says so).
- Remote-input restrictions (PS/Create blocked) are enforced in transport only; with no console
  adapter, nothing on the console is controlled at all.
- Latency is not claimed; see the stopwatch method in the hardware plan.

## Remaining work

1. Hardware stages A, A2, B, C (`SHAREPLAY_HARDWARE_TEST.md`), on a test instance.
2. Measure bandwidth/CPU on the real VPS class with real browsers before enabling there.
3. Merge with the PS5 profile / quick-access / AudioLab branch (`SHAREPLAY_INTEGRATION.md`).
4. Only if a safe, documented virtual-controller or capture mechanism appears: a separate
   experimental ELF and stage D.
5. Optional: TURN for restrictive networks (shared with voice), more viewers after measurement.

## Milestone log

| Commit | Milestone |
| --- | --- |
| `65bd581` | Shared contracts + controller protocol; server module; 21 server tests |
| `8eabbe6` | Web broadcast/viewer/Player 2; browser E2E with real LiveKit |
| `c870a87` | PS5 research; console diagnostic receiver; C tests; PS5 ELF import gate unchanged |
| `8615b53`, `1ff6006` | Architecture + hardware test docs, measurement script, E2E hardening, lint fix |
| next | Opt-in console receivers (older console apps never get input events), integration notes, final status |
