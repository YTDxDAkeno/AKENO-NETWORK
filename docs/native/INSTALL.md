# Install — AKENO NETWORK Native 1.0.0-beta.1

**Beta.** This build has run on PCs only (unit, integration and end-to-end tests against the real
AKENO server). **No 1.0.0-beta.1 file has run on a PS5 yet.** The first console run is the owner's
hardware test ([HARDWARE_TEST_PLAN.md](HARDWARE_TEST_PLAN.md)). The hardware-tested builds are in
`fallback/` ([ROLLBACK.md](ROLLBACK.md)).

## What you need

- A PS5 on **firmware 12.20** with your usual setup: exploit host, **your kstuff**, an ELF loader
  (port 9021) and the AKENO daemon you already paired. Native 0.1 and the Alpha 0.5 RC daemon ran
  on this setup.
- A **homebrew launcher** that starts websrv-format homebrew from `/data/homebrew` as the
  foreground application (websrv's Homebrew Launcher, as with Native 0.1). Starting homebrew this
  way **closes a running game** (launcher behaviour).
- FTP or a USB stick to copy one folder.
- Your phone or a PC with the AKENO website, for comparing.

Nothing in this package installs a HEN, a kstuff, a launcher or a system file.

## What is in the package

| Path | What | Hardware status |
| --- | --- | --- |
| `AKENONative/` | AKENO NETWORK Native 1.0.0-beta.1 for the launcher (`eboot.elf`, `homebrew.js`, `sce_sys/icon0.png`) | **not run on a PS5** |
| `daemon/akeno-network-ps5-native-ipc.elf` | the Alpha 0.5 daemon plus the native interface, protocol 2 (reads and a fixed list of writes) | **not run on a PS5** |
| `akeno-native.elf` | the same program as `AKENONative/eboot.elf`, only for the background safety check | — |
| `experimental/in-game-companion/` | the In-Game Companion (OnionHEN plugin), **experimental, not part of this release** | not run on a PS5; needs OnionHEN (owner decision) |
| `fallback/native-0.1-d094e62bcaf1/` | Native 0.1 | **hardware-tested** (FW 12.20) |
| `fallback/akeno-network-ps5-alpha-0.5-rc-7e28255.elf` | the Alpha 0.5 RC daemon | **hardware-tested** (FW 12.20) |
| `fallback/native-0.2-preview-4b4602c7d0b7/` | Native 0.2 preview and its IPC-1 daemon | not run on a PS5 (rebuilt, byte-verified against CI) |
| `fallback/native-0.1-47488b65f4c2/` | Native 0.1 with the pull-request build ID | not run on a PS5; reference only |
| `docs/`, `CHANGELOG.md`, `RELEASE_NOTES.md`, `LICENSES.md`, `licenses/` | documentation and licences | — |
| `SHA256SUMS`, `BUILDINFO.txt` | checksums, source commit, inputs, gates | — |

## 1. Check the files

On a PC, in the package folder:

```sh
sha256sum -c SHA256SUMS
```

Windows (PowerShell): `Get-FileHash <file> -Algorithm SHA256` and compare with `SHA256SUMS`.
Do not continue if a file differs.

## 2. Back up

Copy `/data/akeno/` (your pairing and settings) and, if present, `/data/homebrew/AKENONative/` and
`/data/akeno-native/` to the PC. Keep the whole package on the PC; `fallback/` is your way back.

## 3. Switch the daemon to the native-IPC build

AKENO Native's social screens (friends, messages, party, inbox, settings) need the
**native-IPC daemon**. It is the Alpha 0.5 daemon with one addition: a local interface on
`127.0.0.1:8642` for AKENO Native, protected by a random key in `/data/akeno/native.key` (0600, new
at every start, deleted at stop). Pairing, server, console name and the server-side version
(`0.5.0-alpha`) stay as they are.

1. If an AKENO daemon runs: open its page and press **Exit AKENO** (confirm). Wait for
   "AKENO NETWORK stopped."
2. Send `daemon/akeno-network-ps5-native-ipc.elf` with your ELF loader, as you send the daemon
   today. Expect "AKENO NETWORK Alpha 0.5 started."
3. Only one daemon runs at a time. "Port 8642 is used by another program" means one was still
   running: repeat step 1.

With the release daemon AKENO Native still starts; the social screens then say what is missing
and the platform checks (controller, audio, connection, system) work.

## 4. Copy AKENO Native

Copy the folder `AKENONative` to `/data/homebrew/AKENONative/` (replace an older one; keep
`sce_sys/icon0.png`). Three files: `eboot.elf`, `homebrew.js`, `sce_sys/icon0.png`.

## 5. Start

Open the homebrew launcher from the home screen (no game running) and start
**AKENO NETWORK — Native 1.0.0-beta.1**. Continue with [FIRST_RUN.md](FIRST_RUN.md).

**Do not send `eboot.elf` or `akeno-native.elf` with the ELF loader** to use it: started that way
it detects that it is a background process, shows one notification, writes its report and stops.

## Files on the console

| Path | Access | Content |
| --- | --- | --- |
| `/data/homebrew/AKENONative/` | installed by you | `eboot.elf`, `homebrew.js`, `sce_sys/icon0.png` |
| `/data/akeno-native/` (0700) | written by AKENO Native | `native.log` (redacted, rotated at 512 KiB), `native-report.txt` (0600) |
| `/data/akeno/settings.json`, `device.json`, `ca.pem` | read only | server address, device token (Connection check only), optional CA |
| `/data/akeno/native.key` | read only | written by the native-IPC daemon; used only if it is a private regular file of 64 hex digits |

AKENO Native never writes, renames or deletes anything in `/data/akeno/`, never pairs or unpairs,
and never changes the server address, the console name or the daemon's secrets.

## Uninstall

Delete `/data/homebrew/AKENONative/` and `/data/akeno-native/`. To return to the release daemon,
see [ROLLBACK.md](ROLLBACK.md).
