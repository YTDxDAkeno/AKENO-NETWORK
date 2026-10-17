# AKENO NETWORK for PS5 — Alpha 0.5 release candidate

Console app `0.5.0-alpha`. A PS5-only update: **no server change, no database migration, no new
server dependency.** It uses the APIs the production server (Alpha 0.4) already has. Nothing in this
release has run on a PS5 yet; see the [hardware test plan](HARDWARE_TEST_PLAN_PS5_ALPHA_0.5.md).

Two ELF files are built from the same source:

| File | What it is |
| --- | --- |
| `akeno-network-ps5.elf` | Release candidate: everything that works today, plus profiles, Quick Access settings and the (off by default) controller shortcut experiment. No audio code. |
| `akeno-network-ps5-audiolab-v2.elf` | The same app plus the experimental **audio lab v2** (Diagnostics). For the audio test only. |

Both use `/data/akeno`: pairing, settings and drafts are shared, so you can switch between them (and
back to the fallback builds) without pairing again. Only one runs at a time.

## New

### Profiles on the console (Profile tab, Friends → View profile)
- **My profile** and **friends' profiles** inside the AKENO page: full-width banner (your uploaded
  banner or the theme's default), large round picture overlapping the banner with the theme's
  accent ring, display name, @Online ID, relationship ("Friends", "You", "Friend request sent", …),
  presence (playing / online / last seen), featured badges as chips with the original AKENO
  medallions and the other badges as medallions, status message, joined date, mutual friend count.
- **Now playing** card with game artwork, **Recently played** shelf (2:3 covers, last played,
  tracked time), **Favorite games** shelf, **Gaming activity** (tracked playtime, games, sessions,
  tracked since), **Party** card (join/open), **About** (bio) and **Mutual friends** (open their
  profiles).
- **Actions** shown only when the server allows them: Message, Invite to party (your party, a
  friend not in it), Add friend, Accept/Decline a friend request, Cancel your request, Join party,
  Back.
- **Privacy:** every value comes from the server for this account (`GET /users/:id` and
  `/users/:id/activity`, "profile" permission). Friends-only profiles show a notice instead of
  details; hidden game history says so; a user who blocked you is indistinguishable from a missing
  profile; profiles are never cached — each open asks the server again.
- **Images:** pictures, banners (WebP 1500 px where the browser supports WebP, else the 960 px
  JPEG), game artwork and this console's own game icons through the console app, as data: URLs —
  the page's strict policy is unchanged and the device credential never reaches the page. A
  bounded, size-limited image cache; missing or undecodable images keep the initials or the theme
  banner and never stop the profile from loading.
- **Controller:** D-pad through actions, cards and game shelves; ✕ selects; ◯ goes back to where
  the profile was opened (and to the previous profile when you came from a mutual friend), with the
  focus restored; the focus ring also shows in browsers without `:focus-visible`.
- Profile editing stays on the website (the server does not allow it to devices).

### Quick Access (Settings)
- A **Quick Access** section lists every way to open AKENO during a game with its verified status.
- **Experimental controller shortcut** (off by default): hold L1 + R1 + OPTIONS (or L3 + R3, or
  touch pad + OPTIONS) for 1–2 s to reopen AKENO's page in the running instance. Read-only
  controller access, loaded only when turned on, debounced, at most once per hold and every 5 s,
  test mode, full diagnostics. **Unverified on hardware** — see [QUICK_ACCESS.md](QUICK_ACCESS.md).
- The page remembers its last screen and unsent drafts across a reload of the browser page.
- Open targets `profile` and `profile:<id>` for links and second launches.

### Audio lab v2 (separate ELF)
- Root-cause research of the silent v1 tone and a much more detailed lab: staged results that
  never claim "audible" without your report, SDL-driver presets (S16 stereo 1024 by default),
  aligned buffers, silence before/after, timed output calls, a delayed test to leave the browser,
  a browser (Web Audio) comparison, per-test feedback and an exportable report. See
  [PS5_AUDIO_RESEARCH.md](PS5_AUDIO_RESEARCH.md).

## Changed
- Friends: selecting a friend opens the profile; **Message** is the button next to it.
- The "Open in AKENO" notification button is described as working (your 12.20 report); it stays
  off by default for new installs.
- Version 0.5.0-alpha; start-up notification "AKENO NETWORK Alpha 0.5 started."

## Unchanged (preserved)
Pairing and its credential, HTTPS/WSS, friends, messages, parties, invitations, game sessions,
notifications (plain and with button), game detection and playtime, local game icons, avatars,
second-launch reopen, one-time notification links, the local page's security model. The release
ELF's dynamic imports are **identical** to the `ae94851` build that ran on your PS5 (CI checks it).

## Safety
- No game process injection, kernel hooks, firmware offsets or input interception; the PS button
  is never touched.
- New system functions (controller) are resolved with `dlopen` only when the user turns the
  shortcut on; missing ones disable only that feature.
- The release ELF contains no audio code and no microphone code exists in any build (CI checks).

## Known limitations
- The controller shortcut may not see any input while a game or the browser is in front; test B
  decides. Fallbacks: notification button, PS button → browser, sending the ELF again.
- PS5 audio from AKENO is not audible as of v1; v2 is a diagnostic build, not a fix.
- Badges show AKENO's own medallions (no badge images exist on the server).
- Mutual friends: the server returns at most six.

## Possible future server tasks (not done, not required)
None of these is needed for this release; they would only extend it later:
1. Optional profile editing from paired consoles (status message only) — would need a new device
   scope and route on the server.
2. Unblocking from the console — the server reserves block management for the website by design.
