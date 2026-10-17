# Hardware test plan — AKENO NETWORK PS5 Alpha 0.5 (release candidate) and audio labs v2 and v3

Results of tests A–C on 12.20 (2026-10-17) are in
[PS5_HARDWARE_RESULTS_FW12.20.md](PS5_HARDWARE_RESULTS_FW12.20.md). **Next: [test D](#test-d--audio-lab-v3-read-only-port-state-and-browser-audio-home-screen-only)**
(audio lab v3).

For **your PS5 with firmware 12.20 (kstuff)**. Firmware 13.60 is not part of this test.
Nothing here is hardware-verified until you run it. Please note the result of every step
(✅ / ❌ / what you saw) and take photos of the screens marked 📷.

## Before you start

1. Download the artifact **akeno-network-ps5-alpha-0.5-rc** from the "PS5 console build" run of the
   commit you approved (see README.md in the artifact). Check the checksums: `sha256sum -c SHA256SUMS`.
2. Files you need:
   - `akeno-network-ps5.elf` — the release candidate (tests A and B); the same file as the one you
     tested (7e28255) apart from its build ID, which CI checks
   - `akeno-network-ps5-audiolab-v3.elf` — experimental audio lab v3 (test D only)
   - `fallback/akeno-network-ps5-audiolab-v2-7e28255.elf` — the audio lab v2 you tested (test C)
   - `fallback/akeno-network-ps5-alpha-0.5-rc-7e28255.elf`, `fallback/akeno-network-ps5-previous-ae94851.elf`,
     `fallback/akeno-network-ps5-audiolab-v1-ae94851.elf`, `fallback/akeno-network-ps5-alpha-0.1.elf`
     — byte-identical rebuilds of builds you already used
3. All builds use `/data/akeno` — your pairing and settings are kept when you switch between them.
   Only one AKENO runs at a time: **Exit AKENO** (top right) before starting another build.
4. If anything misbehaves: Exit AKENO (or restart the console) and send the previous ELF from
   `fallback/`. No build changes system files.

## Test A — Release candidate: everything that worked before, plus profiles

| # | Step | Expected |
| --- | --- | --- |
| A1 | Send `akeno-network-ps5.elf` with your loader from the home screen | Notification "AKENO NETWORK Alpha 0.5 started."; the browser opens the page; badge "Alpha 0.5" |
| A2 | Look at the top bar | Your name, status and **Live** (green). No pairing screen (pairing kept) |
| A3 | **Friends** tab | Friends with presence and "Playing …"; each row shows **View profile** and a **Message** button |
| A4 | Message a friend (Message button) and receive a reply | Messages work as before; a notification appears for the reply |
| A5 | Start a game, wait 30 s, return to the page (PS button → browser) → **Games** tab | Game detected with name and duration, as before |
| A6 | **Profile** tab (or your name in the top bar) | **Your profile**: banner (your uploaded banner or the theme's), large round picture overlapping the banner, display name, @Online ID, "You", badges, status message, presence, Now playing (if a game runs), Recently played, Gaming activity, About, Favorite games 📷 |
| A7 | Check your uploaded picture and banner | Both are your real images (not initials/gradient) |
| A8 | Check badges | Featured badges as chips with the medallion and name; other badges as medallions |
| A9 | Friends → select a friend (✕) | **Friend's profile**; the focus starts on **Message** 📷 |
| A10 | Check the friend's banner, picture, badges, status, "Now playing", "Recently played", favorite games, mutual friends | What the website shows you for this friend (the website's privacy rules apply) |
| A11 | Game artwork | Server artwork where the game is in the AKENO catalogue, else the icon this console has for the title, else initials |
| A12 | Controller: D-pad down/up through the profile, left/right across game cards | The yellow focus ring is always visible; the page scrolls with it; no focus gets stuck |
| A13 | ◯ (Circle) on the friend's profile | Back to Friends, focus on that friend |
| A14 | On a friend's profile: **Message** | Opens the conversation |
| A15 | If you lead or are in a party: **Invite to party** on a friend's profile | Your friend receives the invitation |
| A16 | Open a profile of someone who is not your friend (via mutual friends), if available | Only what they share with non-friends; friends-only profiles show a notice instead of details |
| A17 | Diagnostics → Refresh | Version 0.5.0-alpha; game detection and notifications as before; **Quick access** section shows "off" 📷 |

## Test B — Quick access (shortcut experiment)

Turn the shortcut on only for this test. If anything goes wrong, turn it off (Settings → Quick Access)
or Exit AKENO.

| # | Step | Expected / what to report |
| --- | --- | --- |
| B1 | Settings → **Quick Access** | Four ways in, each with "Works"/"Untested"; the shortcut switch is **off** 📷 |
| B2 | Turn on "Experimental: open AKENO with a controller shortcut" (keep L1 + R1 + OPTIONS, hold 1 s) | Status becomes **reading** (or *unavailable*/*failed* — then photograph Diagnostics → Quick access and skip to B7) |
| B3 | **Test the shortcut (30 s)** while the AKENO page is in front; hold L1 + R1 + OPTIONS for 1–2 s | Report whether "Controller shortcut detected" appears. Diagnostics → Quick access: note *reads*, *samples with buttons*, *detections* 📷 |
| B4 | Press the PS button and go to the **home screen**; wait 5 s; come back to the browser via the PS button | Does the browser still show the AKENO page (same screen, draft kept)? |
| B5 | From the **home screen**, hold L1 + R1 + OPTIONS for 1–2 s | Report: does the AKENO page open? |
| B6 | Start a normal game (single-player, safe moment). Play 1 minute normally | Report: anything unusual in the game (input lag, buttons not working, pauses)? |
| B7 | In the game, hold L1 + R1 + OPTIONS for 1–2 s | Report: does AKENO open? Does the game pause (OPTIONS) or keep running? |
| B8 | If AKENO opened: return to the game with the PS button | Report: is the game still running where you left it? |
| B9 | Keep the combination held for 5 s | AKENO must open **at most once** |
| B10 | Notification route: ask a friend to message you while in the game; select **Open in AKENO** | Opens the conversation (already verified; confirms nothing regressed) |
| B11 | Diagnostics → Quick access 📷, then Settings → turn the shortcut **off** | Status **off** |

If B5/B7 don't open AKENO and "samples with buttons" stays at 0 while you press buttons, the
background payload does not receive controller input — the documented fallback is the
notification button (B10), the PS button switcher (B4) and sending the ELF again.

## Test C — Audio lab v2 (experimental, home screen only)

**Do not** run audio tests while a game is running, and do not try anything with microphones —
both need a separate approval.

| # | Step | Expected / what to report |
| --- | --- | --- |
| C1 | Exit AKENO; send `fallback/akeno-network-ps5-audiolab-v2-7e28255.elf` | Badge "Alpha 0.5 · Audio lab v2"; AKENO works as in test A |
| C2 | Diagnostics → Audio lab v2 → **Check audio libraries** | Libraries and functions with ✓/✗, including the "looked up only" names 📷 |
| C3 | **Play a test tone…** → keep "S16 stereo, 1024" and "Moderate" → **Play** (TV volume normal) | A 1.5 s, 440 Hz tone. Then answer "Did you hear the tone?" honestly 📷 |
| C4 | Look at the result table: *sceAudioOutOutput*, *Timing* ("consumed at playback speed" or "returned faster than real time") | Photograph the table 📷 |
| C5 | **Play in 10 s…** → **Start**, immediately press the PS button and wait on the **home screen** (do not start a game) | Report whether you hear the tone on the home screen; then return and answer the question |
| C6 | **Browser audio test…** → **Play in the browser** | Report whether you hear the browser's tone; answer the question |
| C7 | Optional: repeat C3 with "S16 mono, 256" (as v1) and with a headset on the controller | Report where you heard it (TV / controller headset / USB headset) |
| C8 | **Show report** 📷, and/or copy `/data/akeno/audiolab-report.txt` | Contains no tokens or account data |
| C9 | Exit AKENO; send `akeno-network-ps5.elf` again | Normal AKENO |

The most useful single result is C4 + C5 + C6 together: whether the system paced the samples, and
whether the browser (the app in front) or the home-screen context is audible.

## Test D — Audio lab v3: read-only port state and browser audio (home screen only)

Same rules as test C: home screen, no game running, no microphone. Nothing in test D changes
volume, routing, focus, HDMI output or any setting.

| # | Step | Expected / what to report |
| --- | --- | --- |
| D1 | Exit AKENO; send `akeno-network-ps5-audiolab-v3.elf` | Badge **"Alpha 0.5 · Audio lab v3"**; AKENO works as in test A |
| D2 | Diagnostics → Audio lab v3 → **Check audio libraries** | A row `sceAudioOutGetPortState` ✓/✗ (read-only, on the output test's own port) 📷 |
| D3 | **Play a test tone…** → keep "S16 stereo, 1024" and "Moderate" → **Play**; answer the question | As in C3. Then the **Port state** row: three entries (before playback, during the tone, before closing) 📷 |
| D4 | **Play in 10 s…** → **Start**, go to the home screen with the PS button, come back and answer | As in C5; the port state of that test is recorded too |
| D5 | **Browser `<audio>` test…** → **Play with `<audio>`** (TV volume normal) → answer honestly | The page shows the `play()` result and whether it played to the end; report whether you heard it and where 📷 |
| D6 | **Show report** 📷, and/or copy `/data/akeno/audiolab-report.txt` | Contains the port-state lines, the `<audio>` result and the browser feature line; no tokens or account data |
| D7 | Exit AKENO; send `akeno-network-ps5.elf` again | Normal AKENO |

If AKENO stops responding during D3 (not expected): restart the console and send
`akeno-network-ps5.elf`; report it — the port-state call is then the cause.

## What to send back

Photos 📷, and for each step ✅/❌ with a short note. For B, C and D the Diagnostics sections and the
audio report contain everything needed; none of them contains tokens or passwords. For test D,
the report file from D6 is enough on its own: it contains, per test, the raw port-state return
codes and bytes, the `<audio>` result and the browser feature line.
