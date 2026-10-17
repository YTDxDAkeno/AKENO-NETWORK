# AKENO NETWORK Native for PS5 — 0.2 preview (Phase 2.1)

A native, full-screen AKENO application for a jailbroken PS5: SDL2 at 1920×1080, DualSense
navigation and your AKENO account — **without the system browser**. Native 0.1 proved on a PS5
with firmware 12.20 that this app runs in the foreground next to the AKENO daemon, with the
DualSense working and **audible sound** ([results](PS5_NATIVE_HARDWARE_RESULTS_FW12.20.md)). **0.2 adds the first social screens —
Friends, Profiles and Activity — read through the daemon's local interface**, with live updates
from the daemon's realtime session ([Phase 2 plan](PS5_NATIVE_PHASE2.md)). Messages, parties and
settings follow in 2.2–2.4.

The daemon stays responsible for the server connection, realtime, notifications and game
detection; AKENO Native opens no second realtime session. The daemon's browser page
([PS5_CLIENT.md](PS5_CLIENT.md)) is unchanged and remains the working client and the fallback.

## Status

| Feature | Implemented | Automated tests (PC) | Verified on a PS5 |
| --- | --- | --- | --- |
| Full-screen 1920×1080 UI (SDL2 software renderer), AKENO logo, Inter font | yes | screenshots checked pixel by pixel (headless SDL) | **yes** — 0.1: native 1920×1080 on the TV (12.20) |
| Builds for the PS5 with the pinned SDK and SDL2, reproducibly | yes | CI builds twice and compares; import gate | n/a |
| Runs in the foreground from a homebrew launcher, next to the daemon | yes | — | **yes** — 0.1 ran as foreground application `FAKE00000` (12.20) |
| Background-context detection (sent to elfldr → opens nothing, notifies, exits) | yes | unit tests of the classification | **yes** — 0.1: the protection triggered in the ELF-loader attempts (`native.log`) |
| DualSense input via SDL (`scePadOpen` error shown verbatim) | yes | SDL virtual controller through SDL's real event path | **yes** — 0.1: opened, 2,274 events, 0 dropped; per-button coverage not itemised |
| Spatial focus navigation, list scrolling (D-pad, stick, L1/R1), hold-◯ back, dialogs | yes | unit + UI integration tests | partly — 0.1: 48 focus movements; list scrolling is new in 0.2 |
| Controller disconnect banner (SDL removal; on PS5 also the pad's connected flag) | yes | virtual unplug/plug | **no** |
| Foreground audio test, every return code and pacing recorded | yes | fake system calls; SDL dummy device | **yes — audible**: 0.1, four tests, all heard (voice roadmap V1) |
| Reads the existing pairing read-only; `GET /devices/current`; username on screen | yes | fake server: token sent, file never modified, 401 handled | **yes** — 0.1: HTTP 200, pairing valid, signed in, 14 permissions |
| Daemon liveness probe (`GET /api/instance`) | yes | fake daemon | **yes** — 0.1: "Running, 0.5.0-alpha" |
| **Native interface to the daemon** (key file, hello, state, event ring; GET only) | 0.2 | unit (key file rules, loopback client), fake daemon, **end to end with the real server and the native-IPC daemon** | **no** (test P1, P7.1) |
| **Friends**: presence, games, status, request count; live presence, request and profile updates | 0.2 | fake daemon + end to end (live events over the daemon's realtime session) | **no** (P3, P4) |
| **Profiles**: yours and your friends' (bio, status, badges count, favourites, party, recent games) | 0.2 | fake daemon + end to end | **no** (P5) |
| **Activity**: this console's game (daemon detection), your recent games, friends playing | 0.2 | fake daemon + end to end | **no** (P6) |
| Release daemon (no native interface) recognised, with what to do | 0.2 | fake daemon + end to end with the release daemon | **no** |
| Redacted log and report, private files (0600); no names of other people in them | yes | unit + integration + end to end | **no** |
| Clean exit to the system (`sceSystemServiceLoadExec("exit")`, foreground only) | yes | PC exit paths | **not reported yet** (0.2 test P7.3) |
| Launcher folder with AKENO icon (websrv format) | yes | built in CI | **yes** — 0.1 started as `FAKE00000` |
| Messages (2.2), parties (2.3), settings and pictures (2.4), voice | later | — | — |

Hardware results on firmware 12.20 (owner report, 2026-10-17):
[PS5_NATIVE_HARDWARE_RESULTS_FW12.20.md](PS5_NATIVE_HARDWARE_RESULTS_FW12.20.md). Every native CI
artifact ships the hardware-tested Native 0.1 build (`fallback/native-0.1-d094e62bcaf1/`,
byte-verified) and the hardware-tested Alpha 0.5 RC daemon under `fallback/`.

## Requirements

- PS5 on firmware 12.20 with kstuff and your ELF loader (as for the daemon).
- A **homebrew launcher** that starts websrv-format homebrew from `/data/homebrew` as the foreground
  application (websrv's Homebrew Launcher is the documented one). Read
  [ADR §5](PS5_NATIVE_ADR.md#5-what-the-owner-must-decide-no-action-taken) before installing one:
  installing websrv's launcher makes persistent system changes (websrv's, not AKENO's).
- This console already paired by the AKENO daemon (`/data/akeno`).
- For Friends, Profile and Activity: the **native-IPC daemon** (`daemon/akeno-network-ps5-native-ipc.elf`
  in the artifact) running instead of the release daemon. With the release daemon these screens
  explain what to do; everything else works.

## Install and start (Level B)

1. Download **akeno-native-ps5-0.2.0-preview** from the "PS5 native client" workflow run of the
   approved commit; check `SHA256SUMS` (Windows: `Get-FileHash … -Algorithm SHA256`).
2. Daemon: on the AKENO page press **Exit AKENO**, then send `daemon/akeno-network-ps5-native-ipc.elf`
   with your ELF loader as usual (same pairing, same settings).
3. Copy the folder `AKENONative` to `/data/homebrew/AKENONative/` (keep `sce_sys/icon0.png`).
4. Start **AKENO NETWORK** from the launcher, from the home screen. Starting homebrew this way
   closes a running game (launcher behaviour).
5. Exit with Home → **Exit** (or ◯ on Home → Exit). You return to the PS5.

Sent to the ELF loader instead (`akeno-native.elf`), it detects that it is a background process,
shows one notification, writes its report and stops — it opens no screen, controller or audio and
calls nothing that would affect the foreground application.

Step-by-step hardware test: [HARDWARE_TEST_PLAN_PS5_NATIVE_0.2.md](HARDWARE_TEST_PLAN_PS5_NATIVE_0.2.md)
(0.1: [HARDWARE_TEST_PLAN_PS5_NATIVE_0.1.md](HARDWARE_TEST_PLAN_PS5_NATIVE_0.1.md)).

## Controls

D-pad or left stick: move · ✕ select · ◯ back (on Home: exit dialog) · lists: L1/R1 page,
✕ opens the profile, △ reloads · on the Controller test, every button is shown instead of
acting; hold ◯ for 1 s to return. Keyboard on a PC: arrows,
Enter/Space (✕), Esc/Backspace (◯), Tab (OPTIONS), Q/E (L1/R1).

## Files on the console

| Path | Access | Content |
| --- | --- | --- |
| `/data/homebrew/AKENONative/` | installed by you | `eboot.elf`, `homebrew.js`, `sce_sys/icon0.png` |
| `/data/akeno-native/` (0700) | written | `native.log` (redacted, rotated at 512 KiB to `.1`), `native-report.txt` (0600) |
| `/data/akeno/settings.json`, `device.json`, `ca.pem` | **read only** | the daemon's server address, device token (Connection check only), optional CA |
| `/data/akeno/native.key` | **read only** | written by the native-IPC daemon at every start (0600), deleted when it stops; used only if it is a private regular file of 64 hex digits |

**Uninstall:** delete `/data/homebrew/AKENONative/` and `/data/akeno-native/`. Nothing else exists.
To go back to the release daemon: Exit AKENO on its page and send the release ELF (or
`fallback/akeno-network-ps5-alpha-0.5-rc-7e28255.elf`). To go back to Native 0.1: restore
`fallback/native-0.1-d094e62bcaf1/AKENONative/` (the hardware-tested build).

## Security and privacy

- The device token is read by the existing client library and sent only to your AKENO server over
  verified TLS (mbedTLS 3.6.7, embedded CA bundle, or `/data/akeno/ca.pem` like the daemon).
- The credential file is never written, renamed or deleted — also not on HTTP 401 (the daemon owns
  pairing). CI fails if native code references `akeno_pairing_*`, `akeno_logout`,
  `akeno_forget_credentials` or `akeno_realtime_run`; the integration test checks the file's bytes,
  mtime and inode after a 401.
- No realtime connection, presence update or notification from the native app: the daemon keeps
  sole ownership (no duplicates, no races). Social data comes only through the daemon's local
  interface ([PS5_NATIVE_PHASE2.md](PS5_NATIVE_PHASE2.md)): loopback, a per-launch key from a
  private file, an allow-list in the daemon, and — in 0.2 — GET requests only; the end-to-end test
  proves one realtime session per console.
- The report and log hold states and counts of the social screens, never other people's names,
  IDs or texts; the native key is never logged.
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

# End to end (pnpm build, PostgreSQL in TEST_DATABASE_ADMIN_URL, openssl)
cmake -S clients/ps5 -B clients/ps5/build-asan -DAKENO_SANITIZE=ON -DAKENO_REQUIRE_MBEDTLS=ON
cmake --build clients/ps5/build-asan --target akeno-app akeno-app-native-ipc
node clients/ps5-native/tests/e2e/run.mjs --native clients/ps5-native/build/akeno-native \
  --daemon clients/ps5/build-asan/akeno-app-native-ipc --release-daemon clients/ps5/build-asan/akeno-app

# PS5
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk         # ps5-payload-dev/sdk f5d40f0 (same as the daemon)
clients/ps5-native/scripts/build-sdl-ps5.sh         # ps5-payload-dev/SDL ee4c47d, software renderer
make -C clients/ps5-native/console                  # build-ps5/akeno-native.elf (+ .debug.elf)
clients/ps5-native/console/check-native.sh clients/ps5-native/build-ps5
make -C clients/ps5/console NATIVE_IPC=1             # the native-IPC daemon (docs/PS5_CLIENT.md)
```

Run the PC build headless with `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`; point it at test
data with `AKENO_NATIVE_AKENO_DIR`, `AKENO_NATIVE_DIR`, `AKENO_NATIVE_DAEMON_PORT`; drive it with
`AKENO_NATIVE_SCRIPT` (see `src/core/script.h`; `AKENO_NATIVE_SYNC_DIR` for its `sync` steps).
A PC run is never evidence about the PS5.

## Source layout

| Path | Content |
| --- | --- |
| `src/core/` | portable C11, no SDL, unit-tested: navigation, input, audio test runner, redaction, log, process context, logo rasteriser, PNG writer, text helpers, UI model (screens, lists), test scripts, account parsing and the native key file, social model (`social.c`: friends, profiles, activity, daemon state, events) |
| `src/app/` | SDL2 layer: main loop and workers (`app.c`), screens, drawing and glyph cache (`gfx.c`), network checks (`net.c`), loopback client (`ipc.c`), social worker (`social.c`), report |
| `src/platform/` | `akn_platform.h`; `platform_ps5.c` (the only file with console calls, each with its source) and `platform_host.c` |
| `console/` | PS5 Makefile and the safety gate |
| `package/homebrew.js` | websrv launcher entry |
| `tools/make_icon.c` | generates `icon0.png` from the mark at build time |
| `tests/` | unit tests (C), the UI integration test (Node, headless SDL, fake server and daemons) and the end-to-end test (`tests/e2e`: real server, native-IPC daemon) |

Reused unchanged from the daemon: the device client library (`clients/ps5/src`), the tone
generator (`clients/ps5/app/audio_lab_pcm.c`) and the app-info parser (`game_detect.c`).

## Known limitations

- Only the rows marked yes in the last column of the status table are verified on a PS5.
- Needs a homebrew launcher for the foreground; none is installed by AKENO.
- Starting it closes a running game (launcher behaviour); in-game use is not possible this way.
- Friends, Profile and Activity need the native-IPC daemon; 0.2 only reads (friend requests,
  messages, parties and settings stay on the AKENO page until 2.2–2.4).
- Avatars and banners are shown as initials until 2.4 (pictures need a hardened image decoder).
- While AKENO Native is open the daemon's own notifications still appear (2.2 coordinates them).
- Text uses Inter (Latin, Greek, Cyrillic); other scripts show replacement glyphs.
- Software rendering only (what works for foreground homebrew today); about 1–10 ms per frame on a PC.
- On the PS5 a controller that is switched off is detected through the pad's connected flag, which
  SDL ignores; that path is unverified.
- The touchpad, L2/R2 buttons and analog triggers come from SDL's PS5 mapping (unverified).
