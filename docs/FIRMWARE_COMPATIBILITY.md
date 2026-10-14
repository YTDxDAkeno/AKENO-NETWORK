# Firmware compatibility

**Alpha 0.1 of the console app ran on a PlayStation 5 with firmware 12.20 (user report with
screenshots, 2026-10-13/14). Alpha 0.2 has not run on a console yet. Nothing has been tested on
13.60.** This page tracks what is known, what is assumed, and what must be demonstrated before a
feature is called compatible. Building for a firmware never counts as testing on it.

## Status vocabulary

| Status | Meaning |
| --- | --- |
| Compiled | Builds for that target. Says nothing about runtime behavior. |
| Tested in simulation | Runs on Linux against a real AKENO server (`clients/ps5` tests). |
| Tested on hardware | Run manually on a physical console with the listed firmware and components, results recorded below. |
| Stable on hardware | Repeated hardware tests (at least 5 sessions, including restarts and network loss) without a crash. |

## Matrix

| Item | 12.20 — Alpha 0.1 | 12.20 — Alpha 0.2 | 13.60 | Notes |
| --- | --- | --- | --- | --- |
| Jailbreak environment | kstuff (user) | — | kstuff (user) | Not verified by this project; record versions when testing |
| ELF loader | User's ps5-payload-sdk loader (name/version not recorded) | — | Unknown | Not chosen by this project |
| etaHEN | Not used | Not used | Not used | Not required |
| ps5-payload-sdk `f5d40f0` runtime | **Runs** | Compiled | Compiled; in the SDK offset table | See SAFETY.md |
| Build | `akeno-network-ps5.elf` SHA-256 `35f393c6…6b65` (commit `592c23a`) | Release candidate (`akeno-network-ps5-alpha-0.2-rc` artifact) | — | Alpha 0.1 is rebuilt and verified byte-identical in every PS5 CI run |
| App starts | **Tested on hardware** | Not tested | Not tested | |
| Browser launch (`sceSystemServiceLaunchWebBrowser`) | **Tested on hardware**: opened automatically | Not tested | Not tested | |
| Page renders and runs in the PS5 browser | **Tested on hardware** (full UI) | Not tested (Games tab, banner, new settings) | Not tested | Page script limited to ES2017 syntax by lint |
| HTTPS to the production server (mbedTLS, verified) | **Tested on hardware** | Not tested | Not tested | |
| WebSocket (WSS) realtime | **Tested on hardware** (connected state shown) | Not tested | Not tested | |
| Device pairing | **Tested on hardware** (completed, account signed in) | Not tested | Not tested | |
| Native notifications (`sceKernelSendNotificationRequest`) | **Tested on hardware**, incl. **while a game runs** | Not tested | Not tested | Also the diagnostic ELF |
| Notifications with a button (`sceNotificationSend`, opt-in) | n/a | Not tested | Not tested | Loaded at run time; plain fallback |
| Game detection (`sysctl` + `sceKernelGetAppInfo`) | n/a | Not tested | Not tested | Layout (offset 20/16) decided on the console; see GAME_ACTIVITY.md |
| Game names from metadata files | n/a | Not tested | Not tested | |
| Playtime reporting (heartbeats) | n/a | Not tested | Not tested | |
| Controller input in the page | Used by the user; details not recorded | Not tested | Not tested | Please report D-pad / ✕ / ◯ behaviour |
| Stability (long runs, rest mode, network loss) | Not recorded | Not tested | Not tested | |
| Native Game Base friends, presence, messages, parties, voice, game invites | Unsupported | Unsupported | Unsupported | See PS5_GAMEBASE_RESEARCH.md |

## How compatibility is handled in code

`clients/ps5/include/akeno/caps.h` turns detected **facts** (platform, reported firmware version,
sockets, TLS backend, writable storage) into explicit decisions:

- Network features depend only on generic facts (sockets + verifying TLS + storage), never on a
  firmware number.
- A firmware version is parsed strictly (`MM.mm`). Unknown, unlisted or malformed versions are
  reported as such and **never enable anything**.
- The table of tracked versions (12.20, 13.60) carries a `hardware_tested` flag that is only
  shown in Diagnostics and never enables anything. It is set only together with a recorded
  hardware test of the console app in the log below: 12.20 = 1 since the Alpha 0.1 test, 13.60 = 0.
- System notifications are enabled by a platform fact (a reviewed notification call is linked),
  never by a firmware number.
- There are no firmware offsets, patches or version-specific code paths in this project, and none
  may be added without the review described in [SAFETY.md](SAFETY.md).

Run `akeno-cli caps --firmware 12.20` to see the decisions.

## Console build

Built by the "PS5 console build" GitHub Actions workflow (`.github/workflows/ps5.yml`) with the
pinned SDK; see [PS5_CLIENT.md](PS5_CLIENT.md) for installation and the system functions used,
and [HARDWARE_TEST_PLAN_ALPHA_0.2.md](HARDWARE_TEST_PLAN_ALPHA_0.2.md) for the next hardware
test. Nothing is sent to a console automatically.

## Hardware test log

| Date | Build | Console / firmware | Environment | Steps | Result |
| --- | --- | --- | --- | --- | --- |
| 2026-10-13 | Toolchain diagnostic ELF, Actions run 38476802946 (commit `e324fa6`) | PS5, 12.20 | kstuff; loader not recorded | Sent the ELF with the user's loader | **Pass** (user-reported with screenshot): the system notification "AKENO NETWORK: PS5 toolchain diagnostic loaded. No network connection or system modification was attempted." appeared on the home screen. Proves only that this ELF runs and the notification call works. |
| 2026-10-13/14 | **Console app Alpha 0.1**, `akeno-network-ps5.elf` from the `akeno-network-ps5-alpha-0.1` artifact (commit `592c23a`, SHA-256 `35f393c6…6b65` as recorded for that run) | PS5, 12.20 | kstuff; user's ELF loader (version not recorded) | Sent the ELF; paired with the production server; used the page; received messages | **Pass** (user-reported with screenshots): the ELF started; the system browser opened automatically; the complete AKENO page appeared; the console connected to `https://85.190.112.10.sslip.io` over HTTPS; pairing completed and the user's account signed in; the page showed the connected realtime state; native AKENO message notifications appeared, **also while a game was running**. Not reported: controller details, long-run stability, 13.60. The exact artifact checksum the user sent was not reported back; it is assumed to be the Alpha 0.1 artifact above. |
