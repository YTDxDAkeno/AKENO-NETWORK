# AKENO Audio Lab 3.1 (EXPERIMENTAL)

A standalone, **isolated** PS5 audio-output experiment. It reuses the proven, hardware-relevant code
of Audio Lab v3 (commit `05ad4f3`) **verbatim** — `audio_lab_pcm.*` and `audio_lab_state.*`, and the
output call sequence — and wraps it in a tiny, file-controlled, network-free payload modelled on the
System Explorer.

Full research, the audit verdict, what the experiment can and cannot prove, and the decision tree
toward audible background audio: [`docs/PHASE7_AUDIO_ROUTING_LAB_DE.md`](../../docs/PHASE7_AUDIO_ROUTING_LAB_DE.md)
(German). Owner guides (German): [`package/README-DE.md`](package/README-DE.md),
[`package/HARDWARE-TEST-DE.md`](package/HARDWARE-TEST-DE.md), [`package/SAFETY.md`](package/SAFETY.md).

## Isolation (CI-enforced, `console/check-audiolab.sh`)

- `/data/akeno-audiolab/` only — never `/data/akeno`.
- No network: no socket, no web page, port 8642 unused, no server/LiveKit.
- No microphone; no focus/attach/routing/volume/HDMI function.
- A tone plays only on an explicit `tone` command; nothing plays on its own.
- One instance (flock); a second start is refused, so no two audio sessions ever run in parallel.
- Audio functions are resolved with `dlopen`/`dlsym` (not static imports).

## Why it is not just a repeat

A background payload's tone is already known to be accepted-but-silent, and the report says so. The
one **new** measurement is the opt-in, read-only `sceAudioOutGetPortState` on the test's own port,
which can tell apart "attached to no output device" from "attached at nonzero volume yet silent" —
if the unverified PS4 field layout holds. See the Phase 7 doc.

## Layout

- `src/al.h`, `src/al.c` — model, presets, command parsing, classification (portable, unit-tested)
- `src/al_engine.c` — builds and runs one tone through the backend (portable)
- `src/al_report.c` — report.json / report.txt (German) / status.json
- `src/al_backend_ps5.c` — the only file that calls console functions (read-only; audio dlopen-only)
- `src/al_backend_host.c` — the PC simulation with failure injection
- `src/audio_lab_pcm.*`, `src/audio_lab_state.*` — **verbatim** from Audio Lab v3 (`05ad4f3`)
- `src/main.c` — the single-instance, file-controlled loop with a tone worker and heartbeat
- `console/Makefile` — the PS5 build; `console/check-audiolab.sh` — the import safety gate
- `package/` — the German owner guides and the launcher manifest

## Build

```sh
# PC: the logic + the simulated console, with unit tests (ASan/UBSan)
cmake -S clients/ps5-audiolab -B clients/ps5-audiolab/build -DAL_SANITIZE=ON
cmake --build clients/ps5-audiolab/build
clients/ps5-audiolab/build/akn_al_tests

# PS5: the real ELF with the pinned SDK (no console deployment)
PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk make -C clients/ps5-audiolab/console
clients/ps5-audiolab/console/check-audiolab.sh clients/ps5-audiolab/build-ps5
PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk make -C clients/ps5-audiolab/console delivery
```

A passing PC run is **never** evidence of PS5 behaviour.
