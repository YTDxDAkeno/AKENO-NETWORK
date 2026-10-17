# Hardware test plan — AKENO NETWORK Native 0.2 preview (Phase 2.1)

For **your PS5 with firmware 12.20 (kstuff)**, the same setup that ran Native 0.1 next to the
daemon ([results](PS5_NATIVE_HARDWARE_RESULTS_FW12.20.md)). The questions this test answers:
**do Friends, Profiles and Activity work natively, with live updates, through the daemon's local
interface — while the daemon keeps the only server connection, the notifications and the game
detection?** ([Phase 2 plan](PS5_NATIVE_PHASE2.md)). Nothing here is verified until you run it.

Record ✅ / ❌ / what you saw for every step (template at the end); take photos of the screens
marked 📷. Have your phone (or a PC) with the AKENO website at hand, and ideally a friend who can
come online.

## Before you start

1. **Approve this build.** Download **akeno-native-ps5-0.2.0-preview** from the "PS5 native
   client" workflow run of the commit you approved. Check it with `SHA256SUMS`
   (Windows: `Get-FileHash <file> -Algorithm SHA256`).
2. **What is in the artifact**
   | Path | What |
   | --- | --- |
   | `AKENONative/` | AKENO Native 0.2 for the launcher (`eboot.elf`, `homebrew.js`, `sce_sys/icon0.png`) |
   | `daemon/akeno-network-ps5-native-ipc.elf` | the Alpha 0.5 daemon **plus** the native interface (separate build; send it with your ELF loader) |
   | `fallback/akeno-network-ps5-alpha-0.5-rc-7e28255.elf` | the hardware-tested Alpha 0.5 RC daemon, byte-identical (your rollback) |
   | `fallback/native-0.1-*/AKENONative/` | the Native 0.1 you tested (two copies with different build IDs; use the one whose build ID your System screen showed) |
   | `akeno-native.elf` | for the background safety check only (as N1 in the 0.1 plan) |
3. **What changes on the console compared with what you run today**
   - The native-IPC daemon is the Alpha 0.5 daemon with one addition: at every start it writes a
     random key to `/data/akeno/native.key` (readable by its owner only) and deletes it when it
     stops. With that key AKENO Native may *read* friends, profiles, activity and the daemon's
     state; it may not pair, unpair, change the server or console name, open the browser or stop
     the daemon. Your pairing (`device.json`), settings and server are untouched; the server sees
     the same version (0.5.0-alpha).
   - AKENO Native writes only `/data/akeno-native/` (log, report). It reads `native.key`,
     `settings.json`, `ca.pem` and — for the Connection check only, as in 0.1 — `device.json`.
     **This build sends no change to the server at all** (it has no function for it).
   - No title is registered, no system file changed, no microphone opened, nothing auto-starts.
4. **Only one daemon runs at a time.** If you start a second one while one runs, the second only
   asks the first to show its page and exits — nothing breaks, but the new build does not start.
5. If anything goes wrong: PS button → close AKENO Native; if the console does not respond, hold
   the power button until it beeps twice. Then follow **Rollback** below.

## Test P1 — Switch to the native-IPC daemon

| # | Step | Expected |
| --- | --- | --- |
| P1.1 | If the AKENO daemon runs: open its page and press **Exit AKENO** (confirm) | Notification "AKENO NETWORK stopped." |
| P1.2 | Send `daemon/akeno-network-ps5-native-ipc.elf` with your ELF loader, exactly as before | "AKENO NETWORK Alpha 0.5 started."; the AKENO page opens and shows your account as before 📷 |
| P1.3 | On the page: Friends, Messages, a profile | Work exactly as with the release daemon |
| P1.4 | Send yourself a message from your phone | The daemon's notification appears, as before |

If P1.2 shows "Port 8642 is used by another program", a daemon was still running: repeat P1.1.

## Test P2 — Install AKENO Native 0.2

| # | Step | Expected |
| --- | --- | --- |
| P2.1 | Replace `/data/homebrew/AKENONative/` with the new `AKENONative` folder (FTP; keep `sce_sys`) | three files as before |
| P2.2 | Open the homebrew launcher | Entry **AKENO NETWORK — Native preview 0.2** 📷 |
| P2.3 | Start it (from the home screen, no game running) | Full-screen Home; pill **NATIVE PREVIEW**; under "AKENO SOCIAL": **Live from the AKENO daemon on this console**; the Friends tile shows **"N ONLINE"** 📷 |

## Test P3 — Friends

| # | Step | Expected |
| --- | --- | --- |
| P3.1 | ✕ on **Friends** | Your friends as on the website: online first (playing a game before not playing), then away, busy, offline. Each row: initials, name, @online ID, presence or "Playing <game>". Top line: "N online · M playing · K friend requests waiting" 📷 |
| P3.2 | Move up/down; hold the D-pad | The focus moves row by row and repeats; the panel on the right shows the focused friend (presence, game and for how long, status message) |
| P3.3 | With more than 7 friends: L1 / R1 | Page up / down; "1–7 of N" changes |
| P3.4 | △ | "Loading again..."; the list stays where it was |
| P3.5 | Compare with the website | Same friends, same presence |

## Test P4 — Live updates through the daemon

Keep the Friends screen open.

| # | Step | Expected (within ~2 s) |
| --- | --- | --- |
| P4.1 | A friend opens the AKENO website (or goes offline) | Their dot and line change to Online (Offline · last seen …) without pressing anything |
| P4.2 | A friend changes their status message on the website | The new text appears in the right-hand panel when that friend is focused |
| P4.3 | Someone sends you a friend request | "1 friend request waiting" in the top line |
| P4.4 | A friend starts a game on their PS5 with AKENO | "Playing <game>" on their row |

Accepting or declining requests stays on the AKENO page in this build.

## Test P5 — Profiles

| # | Step | Expected |
| --- | --- | --- |
| P5.1 | On Friends, ✕ on a friend | Their profile: initials, name, @online ID · friend, presence, status, bio, member since, mutual friends, badges count, favourite games, their visible party, now playing and recent games (with play time if they share it) 📷 |
| P5.2 | ◯ | Back on Friends, on the same row |
| P5.3 | Home → **Profile** | **Your** profile (@your ID · you) 📷 |
| P5.4 | A friend whose profile is private | "Details are hidden by this person's privacy settings." |

Pictures (avatars, banners) are Phase 2.4: this build shows initials.

## Test P6 — Activity

| # | Step | Expected |
| --- | --- | --- |
| P6.1 | Home → **Activity** | THIS CONSOLE: "No game running" (starting AKENO Native from the launcher closed any game) or the detection state; YOUR RECENT GAMES with play time; FRIENDS PLAYING NOW 📷 |
| P6.2 | ✕ on a friend playing | Their profile; ◯ returns to Activity |

## Test P7 — Connection, notifications, exit

| # | Step | Expected |
| --- | --- | --- |
| P7.1 | Home → **Connection** | New row **Native interface: Connected to the AKENO daemon · protocol 1, daemon 0.5.0-alpha (…-ps5-native-ipc)**; the rest as in 0.1 📷 |
| P7.2 | With AKENO Native open, send yourself a message from your phone | The daemon's system notification still appears (in this build AKENO Native shows no message banners of its own) |
| P7.3 | System → Save diagnostic report; Home → Exit → Exit | Back to the PS5; start again from the launcher: works |

## Test P8 — Still open from 0.1 (please run them now)

| # | Step | Expected / report |
| --- | --- | --- |
| P8.1 | Controller test: every button, both sticks, L2/R2, hold ◯ 1 s ([0.1 plan, N4](HARDWARE_TEST_PLAN_PS5_NATIVE_0.1.md#test-n4--dualsense-input-part-2-controller)) | Each control lights up; hold ◯ returns |
| P8.2 | Audio test, defaults, then 256 frames at −14 dBFS ([N5](HARDWARE_TEST_PLAN_PS5_NATIVE_0.1.md#test-n5--sound-part-3-milestone-v1)) | **Heard or not heard** — the answer decides the voice roadmap; where you listened |

## Optional — Native 0.2 with the release daemon

Exit the native-IPC daemon (page → Exit AKENO), send the release ELF, start AKENO Native:
Friends shows **"This AKENO daemon has no native interface"** with what to do; Controller, Audio,
Connection and System work as in 0.1.

## What to send back

1. The table below and the photos marked 📷.
2. From `/data/akeno-native/`: `native-report.txt` (new section **[Native interface]**: states and
   counts only — no names of other people) and `native.log`.
3. From `/data/akeno/`: the lines of `akeno.log` that start with `native.` (the daemon's view of
   the interface). Leave `device.json`, `instance.key` and `native.key` on the console.

## Rollback

- **Native:** replace `/data/homebrew/AKENONative/` with `fallback/native-0.1-<build ID>/AKENONative/`
  (or delete it). `/data/akeno-native/` may stay or be deleted.
- **Daemon:** page → **Exit AKENO**, then send `fallback/akeno-network-ps5-alpha-0.5-rc-7e28255.elf`
  (the hardware-tested RC) or the release ELF you used before. `/data/akeno/native.key` is deleted
  when the native-IPC daemon stops; if it was left behind (crash, power loss) it is harmless — the
  release daemon ignores it — and you may delete it.
- Pairing, settings, the server, production and your accounts are never changed by switching.

## Results template

| Step | Result | Notes |
| --- | --- | --- |
| P1.1–P1.4 | | |
| P2.1–P2.3 | | |
| P3.1–P3.5 | | |
| P4.1–P4.4 | | |
| P5.1–P5.4 | | |
| P6.1–P6.2 | | |
| P7.1–P7.3 | | |
| P8.1 controller | | |
| P8.2 audio (heard?) | | where you listened: |
| Optional | | |
