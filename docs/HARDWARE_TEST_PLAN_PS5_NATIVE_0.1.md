# Hardware test plan — AKENO NETWORK Native 0.1 (prototype)

For **your PS5 with firmware 12.20 (kstuff)**. The question this test answers:
**can a native, controller-driven AKENO app run in the foreground, with audible sound, without the
system browser?** ([ADR](PS5_NATIVE_ADR.md)). Nothing here is verified until you run it.

Record ✅ / ❌ / what you saw for every step (template at the end) and take photos of the screens
marked 📷. Run everything from the **home screen** with **no game running**, unless a step says
otherwise.

## Before you start

1. **Approve this build.** Download the artifact **akeno-native-ps5-0.1.0-prototype** from the
   "PS5 native client" workflow run of the commit you approved. Check it:
   - Linux/macOS: `sha256sum -c SHA256SUMS`
   - Windows 11 (PowerShell): `Get-FileHash .\akeno-native.elf -Algorithm SHA256` and
     `Get-FileHash .\AKENONative\eboot.elf -Algorithm SHA256` — both must equal the values in
     `SHA256SUMS` and `BUILDINFO.txt` (the two files are the same ELF).
2. **What is in the artifact:** `akeno-native.elf` (for test N1 only), `AKENONative/` (the
   launcher folder: `eboot.elf`, `homebrew.js`, `sce_sys/icon0.png`), `SHA256SUMS`,
   `BUILDINFO.txt`, this plan, the README, the architecture decision and licences.
3. **What this build changes on the console:** it creates and writes **only**
   `/data/akeno-native/` (log, report). It **reads** `/data/akeno/settings.json` and
   `/data/akeno/device.json` (your existing pairing) and never writes, renames or deletes anything
   in `/data/akeno/`. It installs nothing, registers no title, changes no system file, opens no
   microphone, and contacts only your AKENO server (`GET /api/v1/devices/current`) and the local
   AKENO daemon (`GET /api/instance`).
4. Keep your current AKENO daemon (Alpha 0.5) as it is. If it runs, test N6 also checks it.
5. Set the TV volume to a normal listening level (test N5 plays a moderate beep).
6. **If anything goes wrong:** press the PS button and close the app; if the console does not
   respond, restart it (hold the console's power button until it beeps twice). This build keeps
   nothing running after a restart and writes only its own folder.

## Test N0 — What can start foreground homebrew on your console? (read-only)

The native app must be started by a **homebrew launcher** as the foreground application
([ADR §4](PS5_NATIVE_ADR.md#4-launch-levels)). Check what you already have; **install nothing new
for this test without deciding on [ADR §5](PS5_NATIVE_ADR.md#5-what-the-owner-must-decide-no-action-taken) first.**

| # | Check | Report |
| --- | --- | --- |
| N0.1 | Is there a **"Homebrew Launcher"** tile on the PS5 home screen? (websrv's launcher) | yes / no 📷 |
| N0.2 | From your PC, with your usual payloads running: open `http://<PS5 IP>:8080` | a homebrew launcher page appears / nothing |
| N0.3 | Does your HEN or toolbox (if any) list homebrew from `/data/homebrew`? | yes (which one) / no |
| N0.4 | Which loader do you send ELFs to (name, version, port 9021/9020)? | text |

If **none** of N0.1–N0.3 is available: run **N1 only**, send the results, and decide on ADR §5.
Do not install websrv or a HEN just for this test without that decision.

## Test N1 — Safety check: sent to the ELF loader (background)

This proves that the native app recognises the background context on real hardware and then
**opens nothing** (no screen, controller or sound) and **closes nothing**.

| # | Step | Expected |
| --- | --- | --- |
| N1.1 | From the home screen, send `akeno-native.elf` with your loader, exactly like the daemon | Within a few seconds a notification: "AKENO Native must be started from the Homebrew Launcher …". The screen does not change |
| N1.2 | Look at the home screen and any app you had open | Nothing closed, nothing changed |
| N1.3 | Copy `/data/akeno-native/native-report.txt` to your PC (FTP) | `Context: background process (…)` and `Devices allowed: no` |

If N1.1 shows the AKENO screen instead of a notification, stop and send the report: the
context check disagrees with the expected process type (still safe — see the report).
If **nothing at all** happens (no notification, no `/data/akeno-native/`), the ELF could not start
as a background payload (for example a system library it links is not loadable there). Report it;
it does not decide the launcher test (N3), which runs it as an application.

## Test N2 — Install the launcher folder (Level B)

Only with a launcher from N0 (or one you decided to install).

| # | Step | Expected |
| --- | --- | --- |
| N2.1 | Copy the folder `AKENONative` to `/data/homebrew/AKENONative/` (FTP, keep the `sce_sys` subfolder) | `/data/homebrew/AKENONative/eboot.elf`, `homebrew.js`, `sce_sys/icon0.png` |
| N2.2 | Open the homebrew launcher | An entry **AKENO NETWORK — Native prototype 0.1** with the AKENO icon (blue-violet "A" on dark blue) 📷 |

## Test N3 — Start in the foreground (the critical milestone, part 1: screen)

Starting homebrew from the launcher **closes the running game** (launcher behaviour). Start from
the home screen.

| # | Step | Expected |
| --- | --- | --- |
| N3.1 | Select AKENO NETWORK in the launcher | Within ~5 s: full-screen AKENO Home, **not** the browser: AKENO logo, "NATIVE PROTOTYPE", tiles, button prompts at the bottom 📷 |
| N3.2 | Look at the "Platform checks on this console" card (top right) | "Screen: native 1920x1080" |
| N3.3 | If the screen stays black for more than 15 s | PS button → close the app; send `/data/akeno-native/native.log` |

## Test N4 — DualSense input (part 2: controller)

| # | Step | Expected |
| --- | --- | --- |
| N4.1 | Look at the chips (top right) and the platform card | "Controller" chip green; card "Controller: opened, no input yet" |
| N4.2 | Press the D-pad right/left/up/down | The blue focus ring moves between tiles and follows smoothly; the card changes to "Controller: input received" 📷 |
| N4.3 | Hold the D-pad right | The focus moves once, then repeats after ~0.4 s |
| N4.4 | Move the **left stick** | The focus moves like with the D-pad |
| N4.5 | ✕ on a "SOON" tile (e.g. Friends) | A hint "Friends arrives in Phase 2 …"; nothing opens |
| N4.6 | Open **Controller test**; press ✕ ◯ □ △ L1 R1 L2 R2 L3 R3 OPTIONS and press the touchpad; move both sticks; squeeze L2/R2 slowly | Each control lights up while held; sticks move their dots; L2/R2 bars fill gradually; the press counters count 📷 |
| N4.7 | Hold ◯ for 1 second | The bar fills and you are back on Home (a short ◯ does not leave) |
| N4.8 | **If the controller does not respond:** open Controller test with any input you have, or just photograph it | The red text under CONTROLLER (e.g. `scePadOpen: 0x…`) is the key result 📷 |
| N4.9 | Optional: turn the controller off (Control Center → Accessories) and back on with the PS button | While off: yellow banner "Controller disconnected"; after reconnecting it disappears and navigation works. Report what the system did when you pressed PS |

The PS button belongs to the system; the Create button is not used.

## Test N5 — Sound (part 3: milestone V1)

| # | Step | Expected / report |
| --- | --- | --- |
| N5.1 | Home → **Audio test** → **Play test tone** (defaults: S16 stereo, 1024 frames, moderate −20 dBFS) | A 440 Hz beep for 1.5 s. Then the question **"Did you hear the tone?"** — answer honestly 📷 of the result panel |
| N5.2 | Read the result panel | Report *Outcome* and the *Timing* line (e.g. "accepted and paced at playback speed") |
| N5.3 | If not heard: select **Format** → "S16 stereo, 256 frames (passthrough sample)", **Level** → "Clearer (−14 dBFS)", play again | Heard / not heard |
| N5.4 | Where did you listen? | TV speakers (HDMI) / headset on the controller / USB headset / other |

At most 30 tests per launch, at least 3 s apart; ◯ stops a tone. No microphone is used.

## Test N6 — Connection and your AKENO account

| # | Step | Expected |
| --- | --- | --- |
| N6.1 | Look at Home | "Welcome back, **your display name**" and "@your online ID" (if this console is paired) 📷 |
| N6.2 | Open **Connection** | AKENO daemon: Running · version (if the daemon runs) or Not running; Server: https (host hidden); Pairing: device credential present (private); Account: Signed in as … 📷 |
| N6.3 | **Check again** | Same result; "check 2" in Last check |
| N6.4 | After test N7: send yourself an AKENO message from your phone | The daemon's notification still arrives (the native app did not disturb it) |

## Test N7 — Clean exit and second start

| # | Step | Expected |
| --- | --- | --- |
| N7.1 | Open **System** → **Save diagnostic report** | Hint "Report saved to /data/akeno-native/native-report.txt" 📷 of the System screen (the *Process* line matters) |
| N7.2 | Home → **Exit** → **Exit** | You return to the PS5 (home screen or launcher) within a few seconds, without an error message. Report where you landed |
| N7.3 | Start AKENO Native again from the launcher | It starts again (second launch works) |

## What to send back

1. The table below, filled in, and the photos marked 📷.
2. `/data/akeno-native/native-report.txt` and `/data/akeno-native/native.log` (and `native.log.1`
   if it exists). They contain no tokens, no server address and no IP address (redacted); they do
   contain your AKENO online ID and device name, and the console's firmware version.
3. The loader/launcher names from N0.

## Rollback and uninstall

- Exit AKENO Native (or close it with the PS button).
- Delete `/data/homebrew/AKENONative/` and `/data/akeno-native/`.
- That is all AKENO Native ever creates. `/data/akeno` (pairing, settings), the AKENO daemon, the
  production server and your accounts are untouched; the daemon's own fallback ELFs remain as they
  were. A launcher you installed for this test is removed with that launcher's own procedure.

## Results template

| Step | Result | Notes |
| --- | --- | --- |
| N0.1–N0.4 | | |
| N1.1–N1.3 | | |
| N2.1–N2.2 | | |
| N3.1–N3.3 | | |
| N4.1–N4.9 | | |
| N5.1–N5.4 | | |
| N6.1–N6.4 | | |
| N7.1–N7.3 | | |
