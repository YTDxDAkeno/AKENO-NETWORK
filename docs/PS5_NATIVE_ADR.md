# ADR-0001 — AKENO NETWORK Native for PS5

Status: **accepted**. Phase 1 (Native 0.1) is confirmed on a PS5 with 12.20 (E14, E15);
Phase 2 is in progress. Nothing in this record is verified on a PS5 unless the row says so.
Owner decisions still open are listed at the end.

Labels used throughout: **[HW-AKENO]** measured by the console owner on firmware 12.20 with an
AKENO build · **[HW-ext]** an external project's own hardware claim, not reproduced by us ·
**[SRC]** read in source code, never run · **[HYP]** hypothesis.

## 1. Question

Can AKENO run as a native, controller-driven application on firmware 12.20, with audible
foreground audio, without the PS5 system browser — and how, without weakening the console, the
accounts or production?

## 2. Evidence

| # | Fact | Label |
| --- | --- | --- |
| E1 | The AKENO daemon (`akeno-network-ps5.elf`) runs from elfldr as a **background** process (SceSpZeroConf spawn): HTTPS/WSS, pairing, notifications, game detection and profiles work | [HW-AKENO] |
| E2 | From that background process `scePadOpen` returns `0x809B0081` before any read, for every user and combination | [HW-AKENO] |
| E3 | From that background process `sceAudioOutInit`/`Open`/`Output` all succeed, output is paced at playback speed, and **nothing is audible** (audio lab v1, v2; home screen included) | [HW-AKENO] |
| E4 | The 12.20 system browser has no Web Audio API | [HW-AKENO] |
| E5 | The same `0x809B0081` from another ps5-payload-sdk payload on 13.60 (AnyPS5 issue #3177) | [HW-ext] |
| E6 | Omega's daemon README: *"on the PS5 a background payload has no audio session: `sceAudioOut` opens but nothing is heard … apps launched as bigapp, like the UI, do play"* (2026-10-05). Its in-game music therefore "normally is not heard" | [HW-ext] |
| E7 | Every PS5 homebrew with working pad input and audible output that we found runs as the **foreground application** ("BigApp"): SDL homebrew launched by websrv, Omega's UI, Nativehbl's UI, the passthrough sample app (installed PKG, 12.70) | [HW-ext] |
| E8 | Omega's UI: *"a single full-screen SDL2 app, 1920×1080, with a software renderer: that is what works in the context of homebrew launched by websrv"* | [HW-ext] |
| E9 | websrv's `hbldr` (`src/ps5/hbldr.c`, `7748df9`) creates `/system_ex/app/FAKE00000` (remounting `/system_ex` writable if needed, copying PS Now's `eboot.bin`), **kills the running game** (`sceSystemServiceKillApp`), launches `FAKE00000` for the foreground user, attaches with ptrace, raises the process's privileges and replaces its image with the homebrew ELF. websrv also registers a "Homebrew Launcher" tile (`/user/app/FAKE00000`, `sceAppInstUtilAppInstallTitleDir`) | [SRC] |
| E10 | Nativehbl (native SDL launcher) and Omega rely on the same websrv path and require the installed Homebrew Launcher PKG | [SRC] + README |
| E11 | OnionHEN and etaHEN embed an `hbldr` derived from the same code for their own components; etaHEN's `/data/homebrew` "Payload Homebrew Apps" menu is commented out (`etaHEN_toolbox.xml`). A documented OnionHEN/etaHEN launcher for websrv-format homebrew was **not found** | [SRC] |
| E12 | ps5-payload-dev/SDL (`ee4c47d`, zlib): video `sceVideoOutOpen(0xFF…)` + `sceSystemServiceHideSplashScreen`; pad via `scePadInit/Open/ReadState` (ignores the pad's `connected` flag); audio `sceAudioOutOpen(0xFF, MAIN, …)` and ignores `sceAudioOutOutput` results; its `main` wrapper exits with `sceSystemServiceLoadExec("exit")` | [SRC] |
| E13 | SDL2 builds against the pinned SDK `f5d40f0` with only the software renderer (this session, locally and in CI) | build |
| E14 | Native 0.1 (`d094e62bcaf1-ps5`), started from a homebrew launcher, ran as the **foreground application `FAKE00000`** on 12.20 **while the daemon ran in the background**: it reached the daemon's loopback probe (0.5.0-alpha) and the AKENO server over TLS (`GET /devices/current` → 200 with the existing pairing, read-only; signed in; 14 permissions). Owner report and `native-report.txt`, 2026-10-17, [results](PS5_NATIVE_HARDWARE_RESULTS_FW12.20.md) | [HW-AKENO] |
| E15 | In that foreground process: native video 1920×1080; the DualSense **opened successfully** (2,274 controller events, 48 focus movements, 0 dropped); **four `sceAudioOut` tone tests, all heard** by the owner; sent with the ELF loader instead, the app's background-context protection triggered (`native.log`). Full per-button controller coverage and the clean exit were not itemised | [HW-AKENO] |

**Conclusion.** Controller and audible audio are granted to the **foreground application
process**, not to background payloads. This is now **confirmed for AKENO on 12.20**. The background
daemon gets `0x809B0081` and silent output (E2, E3). The foreground native app opens the
DualSense and plays audible sound (E15). It also coexists with the daemon and shares its pairing
without interfering (E14). E5–E8 agreed in advance.

## 3. Decisions

### D1 — Run the native UI as foreground homebrew, launched by a separate homebrew launcher

`akeno-native.elf` is a websrv-format homebrew (`/data/homebrew/AKENONative/eboot.elf`,
`homebrew.js`, `sce_sys/icon0.png`). It is started by a launcher the owner already uses or
explicitly approves (websrv's Homebrew Launcher is the only documented one, E9–E11).

**AKENO itself never performs process replacement**: no ptrace, no `/system_ex` writes, no
killing of applications, no title registration, no privilege changes. Those belong to the
launcher and are listed in §5 for the owner's decision. The CI gate (`check-native.sh`) fails the
build if AKENO code references any of them.

At start the app classifies its own process context (own app ID from `sceKernelGetAppInfo`
versus `sceSystemServiceGetAppIdOfRunningBigApp`). If it is clearly **not** the foreground
application (e.g. sent to elfldr like the daemon), it opens **no** video, controller or audio
device, writes its context report, shows one system notification and exits **without**
`sceSystemServiceLoadExec` (which would act on the foreground application).

### D2 — Rendering: SDL2 software renderer, 1920×1080

Pinned ps5-payload-dev/SDL `ee4c47d` built from source in CI (static, no OpenGL/Vulkan/Mesa,
software renderer only — E8). No other renderer has PS5 evidence. Text: stb_truetype (public
domain/MIT) with the website's own font, Inter (SIL OFL 1.1). Logo: AKENO's original mark,
rasterised in code from its SVG geometry. **No Sony fonts, sounds or UI assets**; the app does not
read the console's system fonts.

### D3 — Input: SDL GameController, plus a read-only connected-flag poll on PS5

DualSense through SDL's PS5 driver (✕ = confirm, ◯ = back). The `scePadOpen` error text that
SDL reports is shown and logged verbatim — that line answers the E2 question for the foreground.
Because SDL ignores `connected` (E12), the PS5 platform layer polls `scePadGetHandle` +
`scePadReadState` (the handle SDL opened; never opens or closes its own) twice a second to show
"controller disconnected". Spatial focus navigation, key repeat and stick-to-D-pad mapping are
portable, unit-tested code.

### D4 — Audio: direct `sceAudioOut` with full return codes

SDL's audio driver hides every output result (E12), so on PS5 the tone is played by AKENO's
platform layer with the same calls SDL makes, recording init/open/output/close results, pacing,
peak and nonzero PCM — the same measurements as audio lab v2, with **the same tone parameters**
(440 Hz, 1.5 s, 10 ms fades, S16 stereo 1024 frames, −20 dBFS default, ≤ −14 dBFS selectable,
hard cap −6 dBFS), so a foreground result is directly comparable with E3. The tone generator is
the existing unit-tested `clients/ps5/app/audio_lab_pcm.c`, linked unchanged. Nothing plays on
its own; one test at a time; ≥ 3 s apart; ≤ 30 per launch; cancel with ◯; the port is always
closed. **No microphone function is declared, resolved or called** (CI-enforced).

### D5 — Backend: reuse the device pairing read-only; no second realtime connection

The native app reuses the existing C client library (`clients/ps5/src`, mbedTLS 3.6.7, embedded
CA bundle) and the daemon's existing files, **read-only**:

| Data | Owner | Native app |
| --- | --- | --- |
| `/data/akeno/settings.json` (`server`) | daemon | reads |
| `/data/akeno/device.json` (device token, 0600) | daemon (pairing) | read by the library; never written, renamed or deleted |
| `/data/akeno/ca.pem` (optional) | owner | used if present, as the daemon does |
| `/data/akeno-native/` (log, report) | **native app only** | writes |

Calls: `GET /api/v1/devices/current` (existing, documented, device-token; returns account,
scopes, server time) and the daemon's existing unauthenticated liveness probe
`GET http://127.0.0.1:8642/api/instance`. The native app does **not** open the realtime
WebSocket, set presence or show notifications in Phase 1 — the daemon keeps sole ownership, so
there are no duplicate notifications, presence races or competing writers. On HTTP 401 it shows
"not paired" and changes nothing (the daemon deletes credentials on 401; the native app must
not). It never calls `POST /api/instance/show` (that opens the browser). CI fails if native code
references `akeno_pairing_*`, `akeno_logout`, `akeno_forget_credentials` or `akeno_realtime_run`.
**No server change, no new endpoint.**

### D6 — Phase 2 IPC

The native app becomes a client of the daemon instead of a second server client (detailed design,
security analysis and increments: [PS5_NATIVE_PHASE2.md](PS5_NATIVE_PHASE2.md)):

```
Native app ──(127.0.0.1:8642, X-Akeno-Native-Key from /data/akeno/native.key 0600)──▶ daemon
            ◀── event ring: messages, presence, party, notifications, game activity  ───
daemon ──(single HTTPS/WSS session, device token)──▶ AKENO server
```

The daemon already proxies an allow-list of `/api/v1` routes for its browser page and keeps an
event ring of everything its realtime session receives; a second per-launch secret, on a smaller
allow-list, opens the same interface to the native app (polling the ring instead of a separate
event stream). From increment 2.2 the daemon also skips its own toast for a conversation that is
open in the native app (heartbeat), like Omega's `ui-active`. This changes the daemon, so it ships
as a **separate daemon build** (`NATIVE_IPC=1`); the hardware-tested release ELF stays
byte-identical, which CI proves on every commit.

### D7 — HEN integration is optional and later

| | OnionHEN (`b23ffe6`) | etaHEN unified port (`89123ff`) |
| --- | --- | --- |
| Firmware 12.20 | SDK firmware tables include 12.20; no 12.20 hardware report found | 12.20 is an exact profile; owner-confirmed broadly on 13.60 only; startup reports for 11.20/12.40/13.40 |
| Plugin interface | Documented SDK (GPLv3): separate-process plugins, versioned IPC, Toolbox UI pages | etaHEN SDK `.plugin` containers; game `.prx` injection (not acceptable for AKENO) |
| Overlay | ShellUI Toolbox **injection**, in-game monitor bar | ShellUI toolbox **injection**, FPS overlay |
| Bundled kstuff | yes; detects a running kstuff and skips its own (`kstuff_probe.c`) | yes; README: "do not … load a second kstuff" |
| Ports | elfldr 9021 (first hop), own loader 9020 | elfldr 9021, FTP 1337, klog 9081, DPI 9090/12800, RPC 8000 |
| System changes | remounts filesystems, blocks the update partition, injects into ShellUI | unmounts the update partition, injects into ShellUI, optional sandbox `/data` access |
| Licence | GPLv3 | GPLv3 |

The native app **does not depend on either**. An OnionHEN plugin (Phase 4) would be a separate,
GPLv3-licensed process talking to the daemon over the D6 IPC, installed only after the owner
chooses OnionHEN and it is verified on 12.20. Running both HENs, or a second kstuff, is ruled out.

### D8 — Branch and history

`main` is still the initial commit; production commits `2f34c71` and `96d5405` are **not on
GitHub** (checked 2026-10-17). The newest published lines both start at Alpha 0.4 (`ae94851`):
Alpha 0.5 (`claude/laughing-ptolemy-op1d4e`, `2ff6927`, CI green) and SharePlay → Account Bridge
(`claude/project-thread-cx3gpe`, `cb2310d`). This work is based on **Alpha 0.5 `2ff6927`**
(fast-forward of the session branch; no merge, rebase or force push) and is purely additive:
`clients/ps5-native/`, one new workflow, docs, two lines of lint configuration. It touches no
server, web, migration, daemon or existing workflow file, so it merges cleanly into the Account
Bridge line (which changes none of those paths either) once production is published —
`docs/INTEGRATION_ALPHA_0.5.md` still applies unchanged.

**Phase 2.1 update.** The native-IPC daemon adds self-contained `#ifdef AKENO_NATIVE_IPC` blocks to
`clients/ps5/app/{app.c,app_internal.h,app_routes.c,ui_http.c,ui_http.h}`, a `NATIVE_IPC=1`
variant to `clients/ps5/console/Makefile` and an `akeno-app-native-ipc` target to
`clients/ps5/CMakeLists.txt`; the release build compiles exactly as before (CI proves the release
ELF is still the RC file). Alpha 0.5 and the Account Bridge line already both change `app.c`,
`app_internal.h`, `app_routes.c`, the Makefile and the CMake file, so these files are part of the
integration in `docs/INTEGRATION_ALPHA_0.5.md` anyway; the native-IPC blocks merge as separate
hunks (the Account Bridge line does not touch `ui_http.*`). `ps5.yml` and `ci.yml` are unchanged.

## 4. Launch levels

| Level | What | Status | Requires |
| --- | --- | --- | --- |
| A — standalone ELF via the owner's elfldr | The **daemon** (unchanged Alpha 0.5) | [HW-AKENO] | nothing new |
| A′ — native ELF via elfldr | Runs as a background process: by D1 it detects this, opens nothing, notifies and exits | [HW-AKENO]: the protection triggered (E15, from the log) | nothing new |
| B — homebrew launcher entry | `/data/homebrew/AKENONative/` with `eboot.elf`, `homebrew.js`, AKENO `icon0.png`, shown and started by a websrv-format launcher | [HW-AKENO]: ran as foreground application `FAKE00000` (E14, E15) | a homebrew launcher (websrv format), already present on the owner's console |
| C — own PS5 Home-screen tile | An AKENO title ID registered with the system (fPKG via kstuff, or `sceAppInstUtilAppInstallTitleDir` like websrv's own tile) | research only | title registration + PKG/fSELF tooling; separate risk review and approval; never a Sony title ID |

Levels B and C are not equivalent: B adds no system entry for AKENO (websrv's own tile opens the
launcher), C registers a new application with the system.

## 5. What the owner must decide (no action taken)

1. ~~Which homebrew launcher to use for the foreground test.~~ **Resolved 2026-10-17:** the owner's existing websrv-format launcher started Native 0.1 as `FAKE00000` (E14). Kept for reference: If websrv (with its Homebrew
   Launcher PKG) is already installed, nothing new is needed. If not, installing it makes these
   **persistent** changes, all done by websrv, not by AKENO: a registered `FAKE00000` title and
   tile; `/system_ex/app/FAKE00000` (with a copy of PS Now's `eboot.bin`) on first homebrew
   launch, remounting `/system_ex` writable if needed; launching homebrew **closes the running
   game**. AKENO's own files are removed by deleting `/data/homebrew/AKENONative` and
   `/data/akeno-native`. Removing websrv's tile and `/system_ex/app/FAKE00000` is websrv's
   procedure; we have **not** verified it, and deleting from `/system_ex` is itself a
   system-partition write. AKENO's documentation never asks you to install a launcher silently.
2. Whether to pursue Level C (own tile) later.
3. ~~Whether to proceed to Phase 2 (daemon IPC).~~ **Decided 2026-10-17:** the owner asked for
   Phase 2 after E14, incrementally, with the daemon keeping realtime, notifications and game
   detection ([PS5_NATIVE_PHASE2.md](PS5_NATIVE_PHASE2.md)).
4. Microphone research (V3) stays blocked until you approve a separate, local-only test.

## 6. Voice roadmap

V1: audible tone from the foreground app — **met** (E15: four tests, all heard; no microphone).
V2: listen-only party audio — LiveKit
stays the infrastructure; options are a server-side LiveKit→Opus/WSS bridge or native WebRTC;
decided by V1 and measured latency/CPU. V3: microphone API research and a separately approved
local-only level meter (Omega's daemon uses `libSceAudioIn` from a background payload; whether
that captures on 12.20 is [HYP]). V4: two-way. V5: voice during a game — independent problem;
E3/E6 indicate background output is silent, so in-game voice is currently **not** expected to
work. Voice chat is **not** claimed: only V1 is verified.

## 7. Licences

AKENO code: MIT. The PS5 ELF links the SDK runtime (GPLv3+) → the distributed ELF is a GPLv3
combined work (as the daemon). SDL2 (zlib), stb_truetype (public domain / MIT), Inter (SIL OFL
1.1, notice shipped), mbedTLS (Apache-2.0), CA bundle (MPL-2.0). **No code was copied** from
Omega, OnionHEN, etaHEN, websrv, Nativehbl or AnyPS5 (all GPL); they were read for architecture
and published function names only. See [LICENSES.md](LICENSES.md).
