# AKENO System Explorer 6.0 (EXPERIMENTAL)

A standalone, **read-only** PS5 diagnostic payload. It collects evidence about the current process
and application environment from legitimate, available system interfaces, so the next engineering
decision about Game Base and in-game voice rests on measured facts, not speculation. It runs as a
background payload (the same process type as the AKENO daemon), opens **no audio port and no
microphone**, and never changes the system.

Full research, capability matrix, audio findings, ShellUI/Game Base analysis, evidence table,
risks, next experiments and limitations: [`docs/PHASE6_AKENO_SYSTEM_EXPLORER_DE.md`](../../docs/PHASE6_AKENO_SYSTEM_EXPLORER_DE.md) (German).
Owner guides (German): [`package/README-DE.md`](package/README-DE.md),
[`package/HARDWARE-TEST-DE.md`](package/HARDWARE-TEST-DE.md), [`package/SAFETY.md`](package/SAFETY.md).

## Modules

| Module | What it records |
| --- | --- |
| A — process & application | own PID/PPID, process record (sysctl, this PID only), executable identity (`sceKernelGetAppInfo`), foreground app, **process context** (foreground/background), foreground user, resource limits/usage |
| B — libraries & exports | file presence + dlopen + dlsym **look-up only** of audio/user/system/pad/video libraries; quarantines the recording/broadcast libraries that hung dlopen on 12.20 |
| C — audio & microphone | read-only baseline: the decisive process context, prior AKENO hardware evidence (silent background output, `0x809B0081` mic, `0x80260011`), the focus/attach names (never called). No port, no microphone. The active probe is a separate, compile-time-gated build |
| D — ShellUI & Game Base | source-backed findings (onionHEN, etaHEN, LiveShell, ps5-theme, np-fake-signin, offact, AnyPS5), each classified SOURCE_VERIFIED / HARDWARE_VERIFIED / HYPOTHESIS / UNKNOWN |
| E — event timeline | bounded session (default 5 min, hard max 30 min), 1 Hz context sampling, user markers, scheduling-gap observations (not interpreted as suspension), clean shutdown |
| F — reports | `report.json`, `modules.json`, `audio-diagnostics.json`, `events.jsonl`, `system-summary.txt`. Only measured data; a missing measurement is an explicit status, never a fake value. Redacted |

## Layout

- `src/ex.h`, `src/ex.c` — core model, probe/event recording, context classification, orchestration
- `src/ex_process.c`, `ex_libraries.c`, `ex_audio.c`, `ex_shellui.c`, `ex_timeline.c`, `ex_report.c` — modules A–F
- `src/ex_redact.c` — redaction (same policy as the Native client)
- `src/ex_backend_ps5.c` — the only file that calls console functions (read-only)
- `src/ex_backend_host.c` — the PC simulation with failure injection
- `console/Makefile` — the PS5 build; `console/check-explorer.sh` — the import safety gate
- `package/` — the German owner guides and the launcher manifest

## Build

```sh
# PC: diagnostic logic against a simulated console, with unit tests
cmake -S clients/ps5-system-explorer -B clients/ps5-system-explorer/build -DAKN_EX_SANITIZE=ON
cmake --build clients/ps5-system-explorer/build
clients/ps5-system-explorer/build/akn_ex_tests
AKN_EX_QUICK=1 AKN_EX_SIM=home-to-game clients/ps5-system-explorer/build/akeno-system-explorer /tmp/ex

# PS5: the real ELF with the pinned SDK (no console deployment)
PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk make -C clients/ps5-system-explorer/console
clients/ps5-system-explorer/console/check-explorer.sh clients/ps5-system-explorer/build-ps5
PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk make -C clients/ps5-system-explorer/console delivery
```

A passing PC run is **never** evidence of PS5 behaviour. No console deployment is done by any of this.
