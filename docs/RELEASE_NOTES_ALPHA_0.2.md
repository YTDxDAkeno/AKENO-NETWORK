# AKENO NETWORK for PS5 — Alpha 0.2 (release candidate)

**Status: release candidate. Not yet run on a PS5.** Every automated check passes (see
PROJECT_STATUS.md); the hardware test ([HARDWARE_TEST_PLAN_ALPHA_0.2.md](HARDWARE_TEST_PLAN_ALPHA_0.2.md))
decides whether it becomes Alpha 0.2. Alpha 0.1, which ran on firmware 12.20, is included in the
same download as `fallback-alpha-0.1/akeno-network-ps5-alpha-0.1.elf` (byte-identical rebuild).

## New

- **Automatic game activity.** AKENO detects the PS5 or PS4 game running on the console
  (read-only process list) and shares it: friends see "Playing <game> · PlayStation 5 ·
  <duration>" on the website and in the console app, updated live. When you close the game, the
  session ends and the tracked playtime is recorded.
- **Recently played.** Last played, last session, total tracked playtime and sessions per game,
  on the website profile and in the console app's new **Games** tab. Only time AKENO actually
  tracked is shown; nothing before it is estimated.
- **Privacy controls** (website): tracking on/off, who sees the current game, who sees the
  history, **show/hide playtime**, **excluded games**, and invisible mode now also keeps the games
  you played while invisible out of everyone else's history.
- **Quick access to messages:** a "New message from …" banner that takes the focus when you come
  back to the page; starting AKENO again opens the newest unseen conversation; replies you started
  writing are kept when new messages arrive.
- **Experimental button notifications** (off by default): message notifications with an
  "Open in AKENO" button, using one-time local links. If they do not work on your console, AKENO
  automatically uses the proven plain notifications.
- **Allow game activity for an existing console** without pairing again (website → Settings →
  Security → Paired devices).
- Diagnostics: game detection details (incl. the evidence that settles an SDK ambiguity about
  `sceKernelGetAppInfo`), button-notification status and a controlled test.

## Fixed

- **Change server** no longer jumps back to the pairing screen after a few seconds; what you type
  is kept until you save or cancel (◯/Escape cancels).
- A half-written reply was erased when a new message arrived.

## Safety and compatibility

- Same system functions as Alpha 0.1 plus one **optional** read-only function
  (`sceKernelGetAppInfo`, weak import: if a firmware lacks it, only game detection is
  unavailable). `sceNotificationSend` is loaded at run time only when you turn the experiment on.
- No injection, hooks, kernel patches, offsets, PSN changes or system modifications.
- Server: one additive database migration (`0003_activity_privacy`); the Alpha 0.1 server keeps
  working on the upgraded database (checked with `infrastructure/scripts/upgrade-check.mjs`).

## Known limitations

- "Playing" means a game process is running, not that the game is on screen (no verified
  foreground API).
- Game names come from the console's metadata files if a payload can read them (hardware test);
  otherwise the title ID is shown until an administrator names it.
- Consoles paired with Alpha 0.1 need "Allow game activity" on the website once.
- No Control Center card, PS-button shortcut or overlay (no safe, documented mechanism; see
  QUICK_ACCESS.md). Native Game Base, PSN friends, parties and voice remain unsupported.
- Firmware 13.60 is untested.

## Requirements

Firmware 12.20 (Alpha 0.1 verified there) or 13.60 (untested), kstuff, a ps5-payload-sdk
compatible ELF loader (the one you used for Alpha 0.1), an AKENO server running this revision
(older servers work for everything except game activity and the new privacy options).
