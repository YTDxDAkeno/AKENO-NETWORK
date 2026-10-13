# AKENO NETWORK for PS5 — Alpha 0.1

The AKENO NETWORK console app is a homebrew payload for a jailbroken PS5. It connects to **your
own AKENO NETWORK server** over verified HTTPS/WSS, pairs with your account through a code you
approve on the website, and shows friends, presence, messages, invitations, parties and game
sessions in a page that opens in the console's web browser. New messages, friend requests and
invitations appear as PS5 system notifications while it runs.

It is independent of PlayStation Network. It does **not** change the console's own friends
list, parties, voice chat or Game Base (no documented interface exists for that; see
[PS5_GAMEBASE_RESEARCH.md](PS5_GAMEBASE_RESEARCH.md)).

> **Hardware status (2026-10-13): the console app itself has not run on a PS5 yet.**
> What is proven on hardware is that a diagnostic ELF built with the same pinned SDK runs on
> firmware 12.20 (kstuff) and shows a system notification with the same call this app uses.
> Everything else below is tested on Linux only. Please follow the [hardware test plan](#hardware-test-plan)
> and report the results so they can be recorded in [FIRMWARE_COMPATIBILITY.md](FIRMWARE_COMPATIBILITY.md).

## What is tested where

| Feature | Linux (host build, real server) | PS5 hardware |
| --- | --- | --- |
| ELF builds with the pinned SDK, imports only reviewed system functions | n/a | Compiled in CI; not run |
| Verified TLS (mbedTLS 3.6.7, chain + host name, Mozilla roots or your CA file) | 24 end-to-end scenarios | Not tested |
| Device pairing (code on the page and as a notification, approval on the website) | Tested | Not tested |
| Credential and settings storage (mode 0600, in `/data/akeno`) | Tested | Not tested (`/data` writability unverified) |
| Friends, presence, requests, messages, invitations, party and party chat, game sessions | Tested (API and in Chromium) | Not tested |
| Realtime updates with bounded reconnects; sign-out when unpaired on the website | Tested | Not tested |
| System notifications | Printed by the host build | **The call works on 12.20** (diagnostic); not tested from this app |
| Opening the page in the system browser (`sceSystemServiceLaunchWebBrowser`) | n/a | **Not tested** — the key open question |
| Controller navigation in the page | Keyboard-only navigation tested in Chromium | Not tested (depends on the PS5 browser) |
| Clean exit, second launch reopens the page | Tested | Not tested |
| Firmware 13.60 | n/a | Not tested at all |

"Tested on Linux" means the exact same C sources and the same TLS library, built for Linux and
run against a disposable AKENO server with a private certificate authority. The PS5 browser is
not Chromium; layout and script compatibility there are unverified.

## Requirements

- A PS5 you own, already jailbroken with **kstuff**, on firmware **12.20** (confirmed for the
  SDK toolchain) or 13.60 (not yet tested). etaHEN is **not** required and not used.
- An **ELF loader for ps5-payload-sdk payloads** — use the same one that ran the diagnostic
  ELF (for example [ps5-payload-elfldr](https://github.com/ps5-payload-dev/elfldr), which listens
  on port 9021). The loader is not part of AKENO and was not chosen or tested by this project.
  The app keeps running until you exit it; whether your loader stays usable meanwhile is
  untested.
- Your AKENO NETWORK server reachable from the console over **HTTPS** (IPv4; the PS5 SDK's
  resolver has no IPv6). The server must include device pairing (any revision from commit
  `e5bd019` on). **The production VPS revision is unverified**; if it lacks pairing, the app
  says "This server does not support device pairing".
- An account on that server, signed in on a phone or PC to approve the pairing code.

## Install and start

1. Download the artifact **akeno-network-ps5-alpha-0.1** from the "PS5 console build" GitHub
   Actions run of the commit you approved.
2. Check the checksum against `SHA256SUMS` in the artifact (and the value in the run log):
   - Linux/macOS: `sha256sum -c SHA256SUMS`
   - Windows PowerShell: `Get-FileHash .\akeno-network-ps5.elf -Algorithm SHA256`
3. Send `akeno-network-ps5.elf` to the console **with your loader, the same way you sent the
   diagnostic**. With ps5-payload-elfldr from Linux/macOS, for example:
   `socat -u FILE:akeno-network-ps5.elf TCP:<ps5-ip>:9021`. Nothing in this project sends files
   to a console automatically.
4. A notification "AKENO NETWORK Alpha 0.1 started." appears and the browser should open the
   AKENO page. If the browser does not open, AKENO keeps running and tells you by notification.
5. First run: enter your server address (`https://...`) and a name for the console, select
   **Show pairing code**, then on your phone/PC open the shown address (normally
   `https://<your server>/pair`), enter the code, choose permissions and approve. The code is
   also shown as a notification.

Sending the ELF again while AKENO runs does not start a second copy; it asks the running app to
reopen its page. Use **Exit AKENO** (top right) to stop it.

## Using it

- **Friends**: presence and activity, incoming requests (accept/decline), select a friend to chat.
- **Messages**: conversations with unread counts; the on-screen keyboard is the browser's own.
- **Invitations**: party, group and game-session invitations (accept/decline) and recent activity.
- **Party**: your party, members, text chat, leave; join friends' parties or create one. Voice
  chat does not exist in AKENO.
- **Game sessions**: sessions you are in and open ones (read-only in this alpha).
- **Diagnostics**: version, firmware, TLS library, trusted roots, random source, browser and
  notification results, feature report, connection test, recent log (tokens redacted).
- **Settings**: notification switches, unpair this console.
- Status selector (Online/Away/Do not disturb/Invisible) in the top bar; the dot next to "Live"
  shows the realtime connection.

Controller: the page relies on the browser's own controller handling. Arrow keys / D-pad move
focus between controls when the browser forwards them, ✕ activates, ◯ goes back. If the
browser exposes the Gamepad API, the page also reads the D-pad, ✕ and ◯ directly. Which of these
the PS5 browser supports is part of the hardware test.

## Files on the console

All in `/data/akeno` (created with mode 0700):

| File | Content |
| --- | --- |
| `settings.json` | Server address, console name, notification switches (mode 0600). Can be edited via FTP. |
| `device.json` | Device token for this server only (mode 0600). Deleted on unpair or when the server revokes it. |
| `akeno.log`, `akeno.log.1` | Diagnostic log, tokens and device codes redacted; rotated at 512 KiB. |
| `instance.key` | Random key that lets a second launch reopen the page; removed on exit. |
| `ca.pem` (optional) | Put a PEM CA bundle here to trust a private CA instead of the built-in Mozilla roots. |

**Uninstall**: Exit AKENO, unpair the console on the website (Settings → Paired devices), delete
`/data/akeno`. AKENO installs nothing else and changes no system files.

## How it is built and why it is safe to try

- **Verified TLS only.** mbedTLS 3.6.7 with mandatory certificate-chain and host-name (or IP
  SAN) checks, TLS 1.2 minimum. Roots: the Mozilla bundle from certifi 2026.07.22 embedded at
  build time, or your `ca.pem`. There is no switch to disable verification. Randomness comes from
  the kernel CSPRNG (`kern.arandom`, else `/dev/urandom`); if neither works, AKENO stops instead
  of using a weak source. Both inputs are downloaded at pinned versions with SHA-256 checks
  (`clients/ps5/scripts/fetch-deps.sh`).
- **Page on this console only.** The page is served on `127.0.0.1:8642` and is not reachable from
  the network. Every API call needs a random per-launch secret (passed in the URL fragment and
  removed from the address bar), an exact loopback `Host` (DNS-rebinding defence) and a
  same-origin `Origin`; responses carry a strict Content-Security-Policy. The device token never
  reaches the browser: the page can only call an allow-listed set of AKENO API routes, and the
  server additionally enforces the permissions you granted at pairing.
- **Bounded everything.** Request and response sizes, timeouts, reconnect attempts, a
  notification rate limit (burst 5, then one per 3 s).
- **System functions used by AKENO code** (all from samples of the pinned SDK; CI fails the build
  if any other `sce*` function is imported — `clients/ps5/console/check-imports.sh`):

  | Function | Purpose | Hardware status |
  | --- | --- | --- |
  | `sceKernelSendNotificationRequest` | Notifications | Works on 12.20 (diagnostic) |
  | `sceUserServiceInitialize` / `Terminate` | Required before opening the browser | Not tested |
  | `sceSystemServiceLaunchWebBrowser` | Open the page | Not tested |
  | `sceNetResolver*`, `sceNetPool*` | DNS, through the SDK's `getaddrinfo` | Not tested |
  | `kernel_get_fw_version` (SDK runtime, read-only) | Show the firmware version | Not tested |

  plus ordinary BSD socket, file and thread functions of the SDK's libc.

- **What the SDK runtime does (disclosure).** Every payload built with ps5-payload-sdk — including
  the diagnostic that already ran on your console — starts with the SDK's runtime (`crt`), which
  uses the kernel read/write access provided by the loader to raise **its own process's**
  credentials and syscall permissions, using firmware-specific offsets maintained by the SDK
  (12.20 and 13.60 are in its table). AKENO's own code contains no offsets and calls none of the
  SDK's kernel-memory helpers (checked in CI). It does not touch other processes, system files or
  Sony services.

## Hardware test plan

Use a console you can afford to reboot, with your data backed up. Stop at the first problem and
report it. For each step note: works / does not work / what you saw.

| # | Step | Expected |
| --- | --- | --- |
| H1 | Send the ELF with your loader | Notification "AKENO NETWORK Alpha 0.1 started." |
| H2 | Wait 5 s | The browser opens the AKENO page (if not: notification "The browser could not be opened") |
| H3 | Move with D-pad/left stick, press ✕ and ◯ | Report which inputs move the focus or a cursor |
| H4 | Enter the server address and console name with the on-screen keyboard | Pairing screen appears |
| H5 | Show pairing code, approve it on your phone/PC | "paired as …" notification; Friends tab loads |
| H6 | Diagnostics → Test connection | "OK … TLSv1.3/1.2 …", firmware shows 12.20 |
| H7 | Send a message to a friend; have the friend reply | Reply appears in the chat and as a notification |
| H8 | Close the browser, have the friend send another message | Notification still appears |
| H9 | Send the ELF again | The page reopens; no second copy starts |
| H10 | Exit AKENO | Notification "AKENO NETWORK stopped." |
| H11 | Repeat H1–H10 on 13.60 (only after 12.20 works) | Same |

Afterwards please copy `/data/akeno/akeno.log` via FTP (it contains no tokens) and note the
kstuff and loader versions.

## Troubleshooting

| Symptom | Cause / fix |
| --- | --- |
| "The server's certificate could not be verified" | The server's certificate is not from a public CA or its host name differs. Use the exact host from the certificate, or put your CA into `/data/akeno/ca.pem`. |
| "This server does not support device pairing" | The server runs an older revision; update it (see VPS_DEPLOYMENT.md). |
| "Port 8642 is used by another program" | Another payload uses that port. |
| Page says "This page has expired" | The browser lost the page secret (e.g. reopened from history). Send the ELF again. |
| "Live updates off" | Pairing did not grant the `realtime` permission; unpair and pair again with it. |
| "Could not create the data folder" | `/data` is not writable in your environment; report this. |

## Building from source

```sh
clients/ps5/scripts/fetch-deps.sh                      # pinned mbedTLS + CA bundle
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk            # SDK commit f5d40f02770e9b0001dd9e18056eca70b3da8b40
make -C clients/ps5/console                            # -> clients/ps5/build-ps5/akeno-network-ps5.elf
clients/ps5/console/check-imports.sh clients/ps5/build-ps5
```

The Linux build of the same app (`akeno-app`) and its tests: `pnpm test:client`
(see [TESTING.md](TESTING.md)). Source layout: `clients/ps5/src` (client library and both TLS
transports), `clients/ps5/app` (console app, local page server, platform layers),
`clients/ps5/app/web` (the page), `clients/ps5/console` (PS5 build).
