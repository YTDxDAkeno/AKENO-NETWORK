# Quick access: opening AKENO while you play

Goal: during a game, get to AKENO (friends, messages, profiles, game activity, party), then go back
to the game, which keeps running whenever the console allows it — without sending the ELF again.

> **Hardware status (2026-10-14)**
> - Verified on a PS5 with 12.20 (your reports): native message notifications during games;
>   notifications with an **"Open in AKENO"** button that open the conversation; sending the ELF
>   again reopens the running AKENO's page.
> - **Not verified:** the experimental controller shortcut (new, off by default) and whether the
>   browser keeps the AKENO page while a game runs.

## Ways in, and how far each is verified

| Way | How | Status |
| --- | --- | --- |
| **Notification button "Open in AKENO"** | Settings → Notifications → "Message notifications with an 'Open in AKENO' button". A one-time local link (`/t/<ticket>`, single use, 10 min) opens the conversation. | **Verified on 12.20** (your report) |
| **Send the AKENO ELF again** | The second copy finds the running one, asks it to reopen its page (at the newest unseen conversation) and exits. No second instance; pairing, live connection, drafts and workers are kept. | **Verified mechanism** (browser reopen proven since Alpha 0.1) |
| **PS button → web browser** | The console's own app switching (Control Center switcher / home screen). | **Untested**: whether the browser keeps the page while a game runs is test item B4 |
| **Controller shortcut (experimental)** | Hold a combination (default **L1 + R1 + OPTIONS**, 1 s). Off by default. | **Not verified** — see below |
| Newest-message banner | When you come back to the page, "New message from …" takes the focus. | Linux/Chromium tested |

Selecting a notification is **not** a global hotkey: it only works when a notification was shown.

## Controller shortcut: research

| Question | Finding |
| --- | --- |
| Is there a verified controller API? | **Yes, for reading**: the PS5 joystick driver of ps5-payload-dev/SDL (`src/joystick/ps5`, commit `ee4c47d`) declares `scePadInit`, `scePadOpen(userId, type, index, param)`, `scePadGetHandle`, `scePadReadState(handle, PS5_PadData*)`, `scePadClose`, the `PS5_PadData` layout and the button bits. The pinned SDK has `libScePad` as names only. |
| Can a background payload read the DualSense? | **Unknown.** The SDL driver runs in homebrew that is launched *as the foreground application* (see [PS5_AUDIO_RESEARCH.md](PS5_AUDIO_RESEARCH.md)). `libScePad` exports `scePadSetProcessFocus`, `scePadSetForceIntercepted` and `scePadSetProcessPrivilege` (names only), which suggests input is delivered to the process in focus; on the PS4, background processes receive masked ("intercepted") data. AKENO is a background process. |
| Without taking ownership from the game? | Reading uses AKENO's own handle and never writes to the controller (no vibration, light bar, focus, privilege, remapping or trigger calls). Whether opening a handle in a background process can disturb a game is **not known**; hence off by default and a test plan. |
| Can a background payload open the browser? | **Yes** — that is how notifications, second launches and pairing already open the page (`sceSystemServiceLaunchWebBrowser`, verified). |
| PS button | **System-reserved.** AKENO never reads it for a shortcut, never changes it and never hooks the system menu. |
| Safe, documented system shortcut? | No documented system API lets a payload register a shortcut. |

Not used, by rule: game process injection, kernel hooks, firmware offsets, input interception,
Control Center cards or overlays.

## Controller shortcut: implementation

- **Off by default.** Settings → Quick Access → "Experimental: open AKENO with a controller
  shortcut". Combination: L1 + R1 + OPTIONS (default; OPTIONS pauses most games before AKENO opens),
  L3 + R3, or touch pad button + OPTIONS. Hold time: 1, 1.5 or 2 s.
- **Loaded only when on.** `libScePad.sprx` is loaded with `dlopen` by the platform layer when the
  switch is turned on (the same pattern as the button notifications); the app's start-up and its
  dynamic imports are unchanged (CI compares them with the build that ran on a PS5). Missing
  library or function → "unavailable"; a failed call → "failed"; nothing else changes and there is
  no retry loop.
- **Read-only, 20 times a second**, for the foreground user (else the first logged-in user).
- **Debounced** (`app/shortcut.c`, unit-tested): all buttons of the combination held together for
  the hold time; extra buttons don't matter; taps and partial chords never fire; after a detection
  nothing fires again until every combination button is released; at most once every 5 s; a gap in
  samples (read errors) restarts the hold.
- **One instance.** A detection makes the running app reopen its own page, exactly like a second
  launch (newest unseen conversation first): no new process, no new workers, the pairing, live
  connection, game detection and drafts stay as they are.
- **Test mode.** Settings → Quick Access → "Test the shortcut (30 s)": detections are reported on
  the page but nothing is opened.
- **Diagnostics → Quick access**: library, `scePadInit`, user and source, `scePadOpen` (or
  "already open; scePadGetHandle used"), reads and errors, samples with buttons, unknown bits,
  detections, activations with the browser result, and a conflict hint when reads succeed but never
  contain buttons (input probably goes only to the app in front).

What the hardware test decides (test B): whether the payload sees buttons at all, at the home
screen, with the browser in front and with a game in front; whether the game keeps running and is
unaffected. Until then the shortcut stays labelled **untested**.

## Coming back to the same screen

The page remembers the last screen (including an open profile or conversation) and unsent drafts
in its own `sessionStorage`, so a reload of the browser page restores both. Nothing is sent
anywhere.

## Security of quick-open links (unchanged)

Button notifications carry `http://127.0.0.1:8642/t/<ticket>`: a random 128-bit, single-use ticket
valid for 10 minutes; never the device token or page secret. Redeeming it redirects to
`/#t=<page secret>&open=<target>`. Targets are validated in C and in the page (`messages`, `party`,
`friends`, `games`, `invitations`, `profile`, `conversation:<uuid>`, `profile:<uuid>`); the server
still decides what this account may open.

## Tests

Unit (`tests/unit/test_shortcut.c`): combinations, hold, taps, partial chords, extra buttons,
release before re-arming, cooldown, sample gaps, hold limits, single-button masks.
End-to-end with a simulated controller (`AKENO_SIM_PAD`, `tests/integration/run.mjs`): off by
default and validated settings; normal play opens nothing; one open per deliberate hold however
long it is held; cooldown; changed combination and hold time; unseen conversation targeted;
second launch still reuses the instance while the reader runs; test mode opens nothing; off stops
reading; open failure reported once without retries; missing library reported; Settings and
Diagnostics pages in Chromium.
