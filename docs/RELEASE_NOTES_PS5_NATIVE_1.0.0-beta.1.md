# AKENO NETWORK Native 1.0.0-beta.1 — release notes

**Beta, pre-release. Not run on a PS5.** Every result below comes from PC tests (unit,
integration, end-to-end against the real AKENO server, fuzzing, PS5 cross-builds with import
gates). The first console run is the owner's hardware test
([HARDWARE_TEST_PLAN_PS5_NATIVE_1.0.0-beta.1.md](HARDWARE_TEST_PLAN_PS5_NATIVE_1.0.0-beta.1.md)).
This is not a stable public release.

## What it is

The AKENO social app for the PS5 as a native full-screen homebrew application: friends, profiles,
activity, **messages with a controller keyboard**, **parties**, **requests and invitations**,
**notifications** and **settings** — through the AKENO daemon already running on the console,
which keeps the only server connection, the system notifications and the game detection.

## Requirements

- PS5, firmware 12.20, your kstuff and ELF loader, websrv's Homebrew Launcher.
- The **native-IPC daemon from this package** (protocol 2). With an older daemon the social
  screens explain what is missing.
- The console paired with your AKENO server (unchanged).

## Highlights

- Messages: direct, groups and party chat; history; sending with idempotent IDs; read state;
  retry; drafts; no duplicate system notification for the conversation you are reading.
- Controller keyboard drawn by AKENO (no system dialog, no new system function).
- Party: create, invite, chat, accept/decline, leave. Voice is shown honestly as not available in
  AKENO Native; no microphone is used.
- Inbox: friend requests, invitations, notifications.
- Settings: notifications, game activity sharing, online status.
- Game covers from administrator-verified server artwork, also for games not installed.

## Security

- AKENO Native talks only to the daemon on 127.0.0.1 with a per-start key from a private file.
- The daemon allows a fixed list of writes and refuses everything else (pairing, server address,
  console name, the daemon's secrets, account/profile/privacy changes, blocks, group and party
  administration, device management). The server checks the console's permissions again.
- Logs and reports are redacted (no tokens, keys, IDs or other people's texts).

## Known limits

- Not run on a PS5 (none of 1.0.0-beta.1 or 0.2).
- Starting from the launcher closes a running game. In-game access exists only as a separate
  experimental OnionHEN plugin that needs the owner's decision.
- No voice on the PS5 (research, `docs/VOICE.md`).
- No sending of pictures or attachments; text only (up to 500 characters).
- Group administration, party leader changes and kicking stay on the website.

## Package

`AKENONative/`, `daemon/`, `experimental/`, `fallback/` (hardware-tested Native 0.1 and Alpha 0.5
RC daemon, byte-verified), `docs/`, `SHA256SUMS`, `BUILDINFO.txt`, `CHANGELOG.md`,
`RELEASE_NOTES.md`, `LICENSES.md`, `licenses/`. Install: `docs/INSTALL.md`; rollback:
`docs/ROLLBACK.md`.
