# AKENO NETWORK for PS5 — Alpha 0.5 (release candidate)

The AKENO NETWORK console app is a homebrew payload for a jailbroken PS5. It connects to **your
own AKENO NETWORK server** over verified HTTPS/WSS, pairs with your account through a code you
approve on the website, and shows friends, presence, messages, invitations, parties, game
sessions and **the games you play** in a page that opens in the console's web browser. New
messages, friend requests and invitations appear as PS5 system notifications, also while a game
is running. It detects the game you are playing and records your playtime, and since Alpha 0.5 it
shows **full profiles** (yours and your friends') with banners, pictures, badges and game activity.
Release notes: [RELEASE_NOTES_PS5_ALPHA_0.5.md](RELEASE_NOTES_PS5_ALPHA_0.5.md).

It is independent of PlayStation Network. It does **not** change the console's own friends
list, parties, voice chat or Game Base (no documented interface exists for that; see
[PS5_GAMEBASE_RESEARCH.md](PS5_GAMEBASE_RESEARCH.md)).

> **Hardware status (2026-10-14)**
> - **Verified on a PS5 with firmware 12.20** (kstuff, your reports): start-up, the browser page,
>   HTTPS/WSS to the production server, pairing, friends and messages, native notifications on
>   the home screen and during games, notifications with an "Open in AKENO" button that open the
>   conversation, game detection with titles, session duration and recently played, friends' live
>   game activity, local game artwork and custom pictures (builds up to `ae94851`, including the
>   audio lab v1 ELF).
> - **Alpha 0.5 on 12.20** ([results](PS5_HARDWARE_RESULTS_FW12.20.md)): profiles **work**; the
>   controller shortcut **does not** (`scePadOpen` 0x809B0081, off by default); audio lab v2 tones
>   were accepted and paced but **not audible**; the browser has no Web Audio API. Audio lab v3 is
>   not tested yet ([test plan](HARDWARE_TEST_PLAN_PS5_ALPHA_0.5.md), test D).
> - PS5 audio from AKENO: **not audible** (v1, v2); see [PS5_AUDIO_RESEARCH.md](PS5_AUDIO_RESEARCH.md).
> - Firmware 13.60: nothing tested.

## What is tested where

| Feature | Linux (host build, real server) | PS5 hardware (12.20) |
| --- | --- | --- |
| ELF starts, imports only reviewed system functions | n/a | **Yes** (up to `ae94851`); Alpha 0.5 imports exactly the same functions (CI-checked) |
| Page in the system browser, HTTPS/WSS, pairing | Tested | **Works** |
| Friends, messages, invitations, party chat, game sessions, realtime | Tested (API and Chromium) | **Works** |
| Native notifications, also during a game | Printed by the host build | **Works** |
| Notifications with an "Open in AKENO" button | Tested (one-time links, fallback) | **Works** (opens the conversation) |
| Game detection, playtime, recently played, friends' activity, artwork | Tested (simulated process list) | **Works** |
| **Profiles** (own and friends', privacy, images, actions) — Alpha 0.5 | Tested (API, Chromium 1920×1080) | **Works** (avatar, banner, badges, game covers, gaming activity) |
| **Quick Access settings, page memory** — Alpha 0.5 | Tested in Chromium | Not tested |
| **Controller shortcut** (experimental, off by default) — Alpha 0.5 | Tested with a simulated controller | **Does not work**: `scePadOpen` returns 0x809B0081 |
| **Audio lab v2** (separate ELF) | Tested with simulated audio | Accepted and paced, **not audible**; no Web Audio in the browser |
| **Audio lab v3** (separate ELF: port state, `<audio>` test) | Tested with simulated audio and Chromium | Not tested |
| Controller navigation in the page | Keyboard navigation in Chromium | Works (your reports); new profile screens not tested |
| Firmware 13.60 | n/a | Not tested at all |

## Requirements

- A PS5 you own, jailbroken with **kstuff**, on firmware **12.20** (Alpha 0.1 verified) or
  13.60 (untested). etaHEN is **not** required and not used.
- An **ELF loader for ps5-payload-sdk payloads** — the one you used for Alpha 0.1.
- Your AKENO NETWORK server reachable from the console over **HTTPS** (IPv4). Game activity and
  the new privacy settings need a server at this revision (migration `0003`); older servers work
  for everything else, and the console says "Server update needed" in the Games tab.
- An account on that server, signed in on a phone or PC to approve the pairing code.

## Install and start

1. Download the artifact **akeno-network-ps5-alpha-0.5-rc** from the "PS5 console build"
   GitHub Actions run of the commit you approved (see [PS5_ARTIFACT_README.md](PS5_ARTIFACT_README.md)).
   It contains `akeno-network-ps5.elf` (release candidate), `akeno-network-ps5-audiolab-v3.elf`
   (experimental), `fallback/` (the builds you already ran, rebuilt and verified byte-identical),
   `SHA256SUMS`, `BUILDINFO.txt`, a README, the release notes and the hardware test plan.
2. Check the checksum: `sha256sum -c SHA256SUMS` (Linux/macOS) or
   `Get-FileHash .\akeno-network-ps5.elf -Algorithm SHA256` (Windows) and compare with the run log.
3. Send `akeno-network-ps5.elf` with your loader, exactly as you did with Alpha 0.1. Nothing in
   this project sends files to a console automatically.
4. Notification "AKENO NETWORK Alpha 0.5 started." and the browser opens the page. An existing
   pairing in `/data/akeno` is kept.
5. First run only: enter your server (`https://...`) and a console name, select **Show pairing
   code**, approve it on the website (`https://<your server>/pair`). Leave "Share the game you
   are playing" ticked if you want game activity.
6. **Paired with Alpha 0.1?** On the website: Settings → Security → Paired devices → **Allow game
   activity**. No new pairing is needed.

Sending the ELF again while AKENO runs does not start a second copy: the running app reopens its
page — at the newest conversation you have not seen yet, if any. Use **Exit AKENO** to stop it.

**Fallback without the page:** if `/data/akeno/settings.json` names your server, AKENO pairs by
notification when the browser cannot open. Settings file (all keys optional except `server`):

```json
{"version":1,"server":"https://your-akeno-server.example","deviceName":"PlayStation 5","notifyMessages":true,"notifySocial":true,"activityEnabled":true,"actionableNotifications":false}
```

**Going back to Alpha 0.1:** Exit AKENO, send `fallback-alpha-0.1/akeno-network-ps5-alpha-0.1.elf`.
It uses the same `/data/akeno` and ignores the new settings keys.

## Using it

- **Friends**: presence, "Playing <game> · PlayStation 5 · <duration>", requests. Selecting a
  friend opens their **profile**; **Message** sits next to it.
- **Profile** (tab, or your name in the top bar): your profile; friends' profiles open from
  Friends, party members, conversations, friend requests and mutual friends. See
  [PROFILES.md](PROFILES.md#on-the-ps5-console-alpha-05).
- **Messages**: conversations with unread counts. A **New message** banner appears anywhere in the
  app and takes the focus when you come back to the page. Half-written replies are kept.
- **Games**: what this console detects right now and your tracked history (last played, last
  session, total tracked). See [GAME_ACTIVITY.md](GAME_ACTIVITY.md).
- **Invitations**, **Party**, **Game sessions**: as in Alpha 0.1.
- **Diagnostics**: version, firmware, TLS, browser and notification results, **game detection**
  (layout evidence, raw probes, heartbeats), **button notifications** (status, test), connection
  test, recent log (tokens redacted).
- **Settings**: notifications (including the "Open in AKENO" button), **Quick Access** (all ways
  in with their status, the experimental controller shortcut and its test), game activity on this
  console, unpair. See [QUICK_ACCESS.md](QUICK_ACCESS.md).
- Controller: D-pad moves the focus, ✕ selects, ◯ (Escape) goes back — also out of the server
  form and out of a conversation. Text input uses the browser's on-screen keyboard.

## Files on the console

All in `/data/akeno` (mode 0700): `settings.json` (0600), `device.json` (device token, 0600),
`akeno.log` / `.1` (redacted, rotated at 512 KiB), `instance.key` (removed on exit), optional
`ca.pem`. **Uninstall:** Exit AKENO, unpair on the website, delete `/data/akeno`.

## System functions used by AKENO code

All taken from samples of the pinned SDK (`f5d40f0`). CI fails if the ELF imports any other
`sce*` function, if a strong import is not in `console/reviewed-imports.txt` (the import set of
the hardware-tested Alpha 0.1), if an optional import is not weak, or if code other than the
platform layer loads libraries at run time (`console/check-imports.sh`).

| Function | Purpose | How it is linked | Hardware status |
| --- | --- | --- | --- |
| `sceKernelSendNotificationRequest` | Notifications | strong (as Alpha 0.1) | Works on 12.20 |
| `sceUserServiceInitialize` / `Terminate` | Needed to open the browser | strong (as Alpha 0.1) | Works on 12.20 |
| `sceSystemServiceLaunchWebBrowser` | Open the page | strong (as Alpha 0.1) | Works on 12.20 |
| `sceNetResolver*`, `sceNetPool*` | DNS via the SDK's `getaddrinfo` | strong (as Alpha 0.1) | Works on 12.20 |
| `sysctl(KERN_PROC_PROC)` | Process list for game detection (read-only) | libc (as Alpha 0.1's libc set) | Not tested |
| `sceKernelGetAppInfo` | Title ID per process (read-only) | **weak**: missing → detection "unavailable", app still starts | Not tested |
| `sceNotificationSend` | Button notifications (opt-in) | **not linked**: `dlopen("libSceNotification.sprx")` + `dlsym` when enabled | Works on 12.20 |
| `scePadInit/Open/GetHandle/ReadState/Close`, `sceUserServiceGetForegroundUser/GetLoginUserIdList` | Experimental controller shortcut (off by default), read-only | **not linked**: `dlopen("libScePad.sprx")` + `dlsym` only when the user turns it on; declarations of ps5-payload-dev/SDL and websrv | Not tested |
| `kernel_get_fw_version` (SDK runtime) | Show the firmware version | as Alpha 0.1 | Works on 12.20 |
| `sceAudioOutInit/Open/Output/Close`, name lookups in `libSceAudioOut/In.sprx` | **Audio lab ELF only** (experimental): output tests, library check | **not linked, not in the release ELF**: `dlopen` + `dlsym` when the user starts a check | v1: accepted, not audible; v2 not tested — see [PS5_AUDIO_RESEARCH.md](PS5_AUDIO_RESEARCH.md) |

Why the last two are not ordinary imports: the SDK runtime (`crt/rtld_payload.c`) refuses to
start a payload when a needed library or a strong symbol cannot be resolved. A new strong import
could therefore stop the proven app from starting on a firmware that lacks it.

**SDK runtime disclosure.** Every ps5-payload-sdk payload (Alpha 0.1 and the diagnostic
included) starts with the SDK's `crt`, which raises its own process's credentials using the
loader's kernel access and per-firmware offsets maintained by the SDK. AKENO's own code contains
no offsets and calls none of the SDK's kernel-memory helpers (checked in CI); it never touches
another process, system files or Sony services. See [SAFETY.md](SAFETY.md).

## Security of the local page

Page on `127.0.0.1:8642` only; per-launch secret in the URL fragment (removed from the address
bar); exact loopback `Host` and same-origin `Origin`; strict CSP; device token never leaves the
app (allow-listed proxy; the server enforces the device's permissions). Notification buttons use
**one-time links** (`/t/<ticket>`, 128-bit, single use, 10 minutes) instead of the secret; see
[QUICK_ACCESS.md](QUICK_ACCESS.md).

## Troubleshooting

| Symptom | Cause / fix |
| --- | --- |
| Games tab: "Permission needed" | Website → Settings → Security → Paired devices → Allow game activity |
| Games tab: "Server update needed" | The server predates game activity; deploy this revision |
| Games tab: "Game detection unavailable" | The process list or `sceKernelGetAppInfo` is not available; send Diagnostics → Game detection |
| Diagnostics shows layout "conflicting" | Title IDs appeared at both known offsets; AKENO reports nothing. Send the raw probes |
| A game shows as PPSA… instead of its name | The console's metadata files are not readable from the payload; an administrator can name the title on the server |
| No notification after turning on button notifications | Turn the experiment off; plain notifications return |
| "The server's certificate could not be verified" | Use the certificate's exact host name, or put your CA in `/data/akeno/ca.pem` |
| "Port 8642 is used by another program" | Another payload uses that port |
| "This page has expired" | Send the ELF again (keeps the pairing) |

## Building from source

```sh
clients/ps5/scripts/fetch-deps.sh                      # pinned mbedTLS 3.6.7 + CA bundle (SHA-256 checked)
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk            # SDK commit f5d40f02770e9b0001dd9e18056eca70b3da8b40
make -C clients/ps5/console                            # -> clients/ps5/build-ps5/akeno-network-ps5.elf
clients/ps5/console/check-imports.sh clients/ps5/build-ps5
```

The build is reproducible: rebuilding Alpha 0.1 (`592c23a`) locally and in CI gives exactly the
SHA-256 of the build that ran on the console. Linux build and tests: `pnpm test:client` (see
[TESTING.md](TESTING.md)).
