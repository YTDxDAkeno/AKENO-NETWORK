# Changelog — AKENO NETWORK Native (PS5)

Hardware status is stated per version. "PC" means unit, integration and end-to-end tests on a PC;
it is never evidence that something works on a PS5.

## 1.0.0-beta.1 — 2026-10-17 (beta, not run on a PS5)

Requires the native-IPC daemon of the same package (protocol 2).

### Added
- **Messages**: direct chats, groups and the party chat in one list (unread counts, newest first);
  conversation history with older messages on demand; sending with a unique client message ID
  (a retry never stores a message twice); read marking; "Not sent" with retry; drafts per
  conversation; write to a friend from Friends or a profile (opens or creates the conversation).
- **Controller keyboard**: AKENO's own on-screen keyboard (letters, numbers, symbols, accents,
  € £; Shift/Caps, cursor, delete, space, send); USB keyboard text where SDL delivers it. No system
  dialog and no new system function.
- **Party**: create, invite friends, party chat, accept/decline invitations, join, leave (with
  confirmation), members with leader and presence. The VOICE panel states that AKENO Native has
  no voice; it shows who is in voice elsewhere. No microphone is used.
- **Inbox**: friend requests (accept/decline), party and group invitations, notifications with
  "mark as read"; Home badges for new messages, party, invitations and inbox.
- **Settings**: message notifications, friend/invitation notifications, game activity sharing,
  online status (Online, Away, Busy, Invisible); platform checks moved here. Server, pairing,
  console name and the daemon's own settings are listed as not changeable here.
- **Notification coordination**: while a conversation is open, AKENO Native tells the daemon
  (`POST /api/native/focus`, every 8 s); the daemon skips its system notification for that
  conversation only, for at most 20 s after the last signal.
- **Artwork**: one resolver everywhere — administrator-verified server artwork (by title ID, also
  for games that are not installed) → installed icon → placeholder; never by name.
- Profile pictures, banners and badges from the server (pictures decoded by stb_image under size
  limits; fuzz-tested).

### Daemon (native-IPC build, still `0.5.0-alpha` to the server)
- IPC protocol 2: an explicit list of 13 write routes (plus the three settings keys that were
  already allowed); everything else that is not a GET stays refused; `:id` segments are 1–64
  characters of `[A-Za-z0-9-]`, no query strings; each write is logged as its route pattern.
- `POST /api/native/focus` for notification coordination.
- The release daemon ELF stays byte-identical to the hardware-tested Alpha 0.5 RC.

### Fixed
- Vector renderer: float-to-int overflow for huge path bounds (found by fuzzing).
- End-to-end test: assertions follow the redesigned profile (the "Friends" relationship pill and
  empty states) without weakening them.

### Testing
- 1,329 unit checks (ASan/UBSan, Valgrind), 120 UI integration checks, end-to-end tests against
  the real server (messages, keyboard, party, requests, settings, notification suppression,
  artwork of an uninstalled game, IPC 2 refusals), libFuzzer for the image and path decoders,
  clang-tidy, reproducible PS5 builds with import gates.

### Experimental (separate, not part of the release)
- In-Game Companion (`experimental/in-game-companion/AKEN00001.elf`): an OnionHEN Toolbox page
  with friends, unread messages, party, invitations and status. Needs OnionHEN (owner decision);
  never run on a PS5. See `docs/IN_GAME_ACCESS.md`.

## 0.2.0-preview — build 4b4602c7d0b7 (not run on a PS5)
- Friends, profiles and activity through the daemon's native interface (protocol 1, GET only).
- Live presence and friend-request updates from the daemon's event ring.

## 0.1 — build d094e62bcaf1 (hardware-tested, FW 12.20)
- Foreground homebrew app from the websrv launcher: 1920×1080 video, DualSense input, audible
  audio tests, account through the console's pairing, background protection when sent with an
  ELF loader. See `docs/PS5_NATIVE_HARDWARE_RESULTS_FW12.20.md`.
