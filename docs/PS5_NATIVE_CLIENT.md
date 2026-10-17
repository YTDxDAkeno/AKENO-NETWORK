# AKENO NETWORK Native for PS5 — prototype 0.1

A native, full-screen AKENO application for a jailbroken PS5: SDL2 at 1920×1080, DualSense
navigation, a foreground audio test and your AKENO account — **without the system browser**.
This first prototype exists to answer one question on real hardware: does a foreground native app
get the controller and audible sound that the background daemon is refused
([ADR-0001](PS5_NATIVE_ADR.md))? The social screens (friends, messages, parties, profiles) come in
Phase 2, on top of what this proves.

The existing AKENO daemon and its browser page ([PS5_CLIENT.md](PS5_CLIENT.md)) are unchanged and
remain the working client and the fallback.

## Status

| Feature | Implemented | Automated tests (PC) | Verified on a PS5 |
| --- | --- | --- | --- |
| Full-screen 1920×1080 UI (SDL2 software renderer), AKENO logo, Inter font | yes | screenshots checked pixel by pixel (headless SDL) | **yes** — shown on the TV (12.20) |
| Builds for the PS5 with the pinned SDK and SDL2, reproducibly | yes | CI builds twice and compares; import gate | n/a |
| Background-context detection (sent to elfldr → opens nothing, notifies, exits) | yes | unit tests of the classification | **no** (test N1) |
| DualSense input via SDL (`scePadOpen` error shown verbatim) | yes | SDL virtual controller through SDL's real event path | partly — navigation reached the Connection screen; full test N4 not reported |
| Spatial focus navigation, D-pad repeat, left stick, hold-◯ back, dialogs | yes | unit + UI integration tests | **no** |
| Controller disconnect banner (SDL removal; on PS5 also the pad's connected flag) | yes | virtual unplug/plug | **no** |
| Foreground audio test, every return code and pacing recorded | yes | fake system calls; SDL dummy device | **not reported yet** (test N5) |
| Reads the existing pairing read-only; `GET /devices/current`; username on screen | yes | fake server: token sent, file never modified, 401 handled | **yes** — HTTP 200, pairing valid, signed in, 14 permissions (12.20) |
| Daemon liveness probe (`GET /api/instance`) | yes | fake daemon | **yes** — "Running, 0.5.0-alpha" (12.20) |
| Native app in the foreground and the daemon in the background at the same time | yes | — | **yes** (12.20) |
| Redacted log and report, private files (0600) | yes | unit + integration (no token, host or IP in output) | **no** |
| Clean exit to the system (`sceSystemServiceLoadExec("exit")`, foreground only) | yes | PC exit paths | **no** (test N7) |
| Launcher folder with AKENO icon (websrv format) | yes | built in CI | **no** (test N2) |
| Friends, messages, parties, profiles, voice | Phase 2+ | — | — |

Hardware results on firmware 12.20 (owner report, 2026-10-17):
[PS5_NATIVE_HARDWARE_RESULTS_FW12.20.md](PS5_NATIVE_HARDWARE_RESULTS_FW12.20.md). The tested
build is shipped as a byte-verified fallback in every native CI artifact (`fallback/`).

## Requirements

- PS5 on firmware 12.20 with kstuff and your ELF loader (as for the daemon).
- A **homebrew launcher** that starts websrv-format homebrew from `/data/homebrew` as the foreground
  application (websrv's Homebrew Launcher is the documented one). Read
  [ADR §5](PS5_NATIVE_ADR.md#5-what-the-owner-must-decide-no-action-taken) before installing one:
  installing websrv's launcher makes persistent system changes (websrv's, not AKENO's).
- For the account check: this console already paired by the AKENO daemon (`/data/akeno`).

## Install and start (Level B)

1. Download **akeno-native-ps5-0.1.0-prototype** from the "PS5 native client" workflow run of the
   approved commit; check `SHA256SUMS` (Windows: `Get-FileHash … -Algorithm SHA256`).
2. Copy the folder `AKENONative` to `/data/homebrew/AKENONative/` (keep `sce_sys/icon0.png`).
3. Start **AKENO NETWORK** from the launcher, from the home screen. Starting homebrew this way
   closes a running game (launcher behaviour).
4. Exit with Home → **Exit** (or ◯ on Home → Exit). You return to the PS5.

Sent to the ELF loader instead (`akeno-native.elf`), it detects that it is a background process,
shows one notification, writes its report and stops — it opens no screen, controller or audio and
calls nothing that would affect the foreground application.

Step-by-step hardware test: [HARDWARE_TEST_PLAN_PS5_NATIVE_0.1.md](HARDWARE_TEST_PLAN_PS5_NATIVE_0.1.md).

## Controls

D-pad or left stick: move · ✕ select · ◯ back (on Home: exit dialog) · on the Controller test,
every button is shown instead of acting; hold ◯ for 1 s to return. Keyboard on a PC: arrows,
Enter/Space (✕), Esc/Backspace (◯), Tab (OPTIONS), Q/E (L1/R1).

## Files on the console

| Path | Access | Content |
| --- | --- | --- |
| `/data/homebrew/AKENONative/` | installed by you | `eboot.elf`, `homebrew.js`, `sce_sys/icon0.png` |
| `/data/akeno-native/` (0700) | written | `native.log` (redacted, rotated at 512 KiB to `.1`), `native-report.txt` (0600) |
| `/data/akeno/settings.json`, `device.json`, `ca.pem` | **read only** | the daemon's server address, device token, optional CA |

**Uninstall:** delete `/data/homebrew/AKENONative/` and `/data/akeno-native/`. Nothing else exists.

## Security and privacy

- The device token is read by the existing client library and sent only to your AKENO server over
  verified TLS (mbedTLS 3.6.7, embedded CA bundle, or `/data/akeno/ca.pem` like the daemon).
- The credential file is never written, renamed or deleted — also not on HTTP 401 (the daemon owns
  pairing). CI fails if native code references `akeno_pairing_*`, `akeno_logout`,
  `akeno_forget_credentials` or `akeno_realtime_run`; the integration test checks the file's bytes,
  mtime and inode after a 401.
- No realtime connection, presence update or notification from the native app: the daemon keeps
  sole ownership (no duplicates, no races). Phase 2 moves the native app onto the daemon via local
  IPC ([PS5_NATIVE_PHASE2.md](PS5_NATIVE_PHASE2.md)).
- Logs and reports pass through redaction: `ak?_` secrets, `Bearer` values, `X-Akeno-*-Key`
  headers, 32+ digit hex runs and every non-loopback URL host. The server address is described,
  never printed ("https (host hidden)").
- No microphone function is declared or called; no launch, kill, registration, mounting, ptrace,
  kernel helper, pad focus or browser call exists in AKENO code (CI gate `console/check-native.sh`).

## Building from source

```sh
clients/ps5-native/scripts/fetch-deps.sh            # stb_truetype, Inter, mbedTLS, CA bundle (SHA-256 checked)

# PC (development, tests): needs SDL2 >= 2.0.18 development files
cmake -S clients/ps5-native -B clients/ps5-native/build -DAKN_SANITIZE=ON
cmake --build clients/ps5-native/build
ctest --test-dir clients/ps5-native/build
node clients/ps5-native/tests/integration/run.mjs clients/ps5-native/build/akeno-native

# PS5
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk         # ps5-payload-dev/sdk f5d40f0 (same as the daemon)
clients/ps5-native/scripts/build-sdl-ps5.sh         # ps5-payload-dev/SDL ee4c47d, software renderer
make -C clients/ps5-native/console                  # build-ps5/akeno-native.elf (+ .debug.elf)
clients/ps5-native/console/check-native.sh clients/ps5-native/build-ps5
```

Run the PC build headless with `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`; point it at test
data with `AKENO_NATIVE_AKENO_DIR`, `AKENO_NATIVE_DIR`, `AKENO_NATIVE_DAEMON_PORT`; drive it with
`AKENO_NATIVE_SCRIPT` (see `src/core/script.h`). A PC run is never evidence about the PS5.

## Source layout

| Path | Content |
| --- | --- |
| `src/core/` | portable C11, no SDL, unit-tested: navigation, input, audio test runner, redaction, log, process context, logo rasteriser, PNG writer, text helpers, UI model, test scripts, account parsing |
| `src/app/` | SDL2 layer: main loop and workers (`app.c`), screens, drawing and glyph cache (`gfx.c`), network checks (`net.c`), report |
| `src/platform/` | `akn_platform.h`; `platform_ps5.c` (the only file with console calls, each with its source) and `platform_host.c` |
| `console/` | PS5 Makefile and the safety gate |
| `package/homebrew.js` | websrv launcher entry |
| `tools/make_icon.c` | generates `icon0.png` from the mark at build time |
| `tests/` | unit tests (C) and the UI integration test (Node, headless SDL) |

Reused unchanged from the daemon: the device client library (`clients/ps5/src`), the tone
generator (`clients/ps5/app/audio_lab_pcm.c`) and the app-info parser (`game_detect.c`).

## Known limitations

- Only the rows marked yes in the last column of the status table are verified on a PS5.
- Needs a homebrew launcher for the foreground; none is installed by AKENO.
- Starting it closes a running game (launcher behaviour); in-game use is not possible this way.
- Text uses Inter (Latin, Greek, Cyrillic); other scripts show replacement glyphs.
- Software rendering only (what works for foreground homebrew today); about 2–10 ms per frame on a PC.
- On the PS5 a controller that is switched off is detected through the pad's connected flag, which
  SDL ignores; that path is unverified.
- The touchpad, L2/R2 buttons and analog triggers come from SDL's PS5 mapping (unverified).
