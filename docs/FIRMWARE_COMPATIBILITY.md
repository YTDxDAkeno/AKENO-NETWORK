# Firmware compatibility

**Only the toolchain diagnostic ELF has run on a PlayStation 5 (firmware 12.20, user-reported).
The console app has not.** This page tracks what is known, what is assumed, and what must be
demonstrated before any feature is called compatible.

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
| ELF/payload loader | Used by the user; name/version not recorded | Unknown | Any ps5-payload-sdk compatible loader; not chosen by this project. |
| etaHEN | Not required | Not required | Neither the app nor the diagnostic uses etaHEN. |
| ps5-payload-sdk `f5d40f0` runtime | In the SDK's offset table; **diagnostic ran** | In the SDK's offset table; not tested | The SDK runtime adjusts its own process's credentials at start (see SAFETY.md). |
| Toolchain diagnostic ELF (notification only) | **Tested on hardware** (user-reported, screenshot) | Not tested | CI artifact of run 38476802946. |
| Console app Alpha 0.1 (`akeno-network-ps5.elf`) | Compiled (CI); **not run** | Compiled (CI); **not run** | Same source tested on Linux: 39 end-to-end scenarios. |
| System notifications (`sceKernelSendNotificationRequest`) | **Call works** (diagnostic) | Not tested | |
| Opening the page (`sceSystemServiceLaunchWebBrowser`) | Not tested | Not tested | Main open question. |
| HTTPS/WSS (mbedTLS 3.6.7, verified) | Not tested | Not tested | Tested on Linux with the identical code. |
| DNS (`getaddrinfo` → `sceNetResolver`, IPv4) | Not tested | Not tested | |
| Credential storage in `/data/akeno` | Not tested | Not tested | |
| Controller input in the page | Not tested | Not tested | Depends on the PS5 browser. |
| Native Game Base friends, presence, messages, parties, voice, game invites | Unsupported | Unsupported | See [PS5_GAMEBASE_RESEARCH.md](PS5_GAMEBASE_RESEARCH.md). |

## How compatibility is handled in code

`clients/ps5/include/akeno/caps.h` turns detected **facts** (platform, reported firmware version,
sockets, TLS backend, writable storage) into explicit decisions:

- Network features depend only on generic facts (sockets + verifying TLS + storage), never on a
  firmware number.
- A firmware version is parsed strictly (`MM.mm`). Unknown, unlisted or malformed versions are
  reported as such and **never enable anything**.
- The table of tracked versions (12.20, 13.60) carries `hardware_tested = 0`. It is changed only
  together with a recorded hardware test of the client in this document (the diagnostic ELF
  does not count as a client test).
- System notifications are enabled by a platform fact (a reviewed notification call is linked),
  never by a firmware number.
- There are no firmware offsets, patches or version-specific code paths in this project, and none
  may be added without the review described in [SAFETY.md](SAFETY.md).

Run `akeno-cli caps --firmware 12.20` to see the decisions.

## Console build

Built by the "PS5 console build" GitHub Actions workflow (`.github/workflows/ps5.yml`) with the
pinned SDK; see [PS5_CLIENT.md](PS5_CLIENT.md) for installation, the list of system functions
used, and the hardware test plan (H1–H11). Nothing is sent to a console automatically.

## Hardware test log

| Date | Build | Console / firmware | Environment | Steps | Result |
| --- | --- | --- | --- | --- | --- |
| 2026-10-13 | Toolchain diagnostic ELF, Actions run 38476802946 (commit `e324fa6`) | PS5, 12.20 | kstuff; loader not recorded | Sent the ELF with the user's loader | **Pass** (user-reported with screenshot): the system notification "AKENO NETWORK: PS5 toolchain diagnostic loaded. No network connection or system modification was attempted." appeared on the home screen. Proves only that this ELF runs and the notification call works. |
