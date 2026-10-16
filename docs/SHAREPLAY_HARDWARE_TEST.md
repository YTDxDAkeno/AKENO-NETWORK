# SharePlay — real-hardware test plan (Alpha 0.5, experimental)

Everything in SharePlay has been tested **only automatically** (Linux container, Chromium,
LiveKit 1.12.0, generated test video, a scripted gamepad). Nothing has run on a PS5, on a real
controller, between two homes or on the production server. These stages turn that into real
evidence. Run them in order; each stage only proves what it says.

**Do not deploy to production for these tests.** Use a separate test instance (or your own
PC running the dev stack, see `docs/LOCAL_SETUP.md`) with SharePlay switched on:

```sh
LIVEKIT_URL=…  LIVEKIT_API_KEY=…  LIVEKIT_API_SECRET=…     # as for voice (docs/VOICE.md)
SHAREPLAY_ENABLED=true
SHAREPLAY_REMOTE_INPUT_ENABLED=true     # only for stage B and later
SHAREPLAY_MAX_VIEWERS=4
SHAREPLAY_MAX_QUALITY=720p60
```

Write down for every stage: date, browser + version, OS, network (same Wi-Fi / different
homes), what you saw, screenshots of the SharePlay section (its statistics line is the
measurement).

## Stage A — browser streaming (two PCs or a PC and a phone)

Proves: real capture → LiveKit → real viewer, with sound, next to voice. Not PS5 capture.

1. Both sign in, join the same party, both **Join voice**, talk briefly.
2. PC 1: Parties → SharePlay → *Stream from: A window or screen*, *Quality: 720p · 30 fps* →
   **Start SharePlay** → read the warning → *Choose what to share* → pick **one window** (a
   video player or a game on the PC), tick *Share audio* if Chrome offers it.
   - [ ] The red "You are sharing: …" indicator appears; the sidebar shows "You are streaming".
3. PC 2 / phone: **Watch stream**.
   - [ ] Live picture; game/video sound; the statistics line shows resolution, fps, bitrate,
     RTT and buffer.
   - [ ] Mute, volume slider, full screen (double-click the picture or the button).
   - [ ] Voice still works both ways while watching.
4. Viewer navigates to Friends and back: sound continues, the bar says "Watching SharePlay",
   the picture returns on the party page.
5. Viewer: **Leave stream**; voice stays connected. **Watch stream** again.
6. Streamer: change quality to 480p; viewer's resolution drops within ~10 s.
7. Streamer: Chrome's own **Stop sharing** button → viewer sees the stream end or wait;
   **Stop SharePlay** → capture indicator disappears immediately; viewer sees "The stream has ended."
8. Reconnection: while watching, turn the viewer's Wi-Fi off for ~5 s, then on → it shows
   *Reconnecting…* and recovers.
9. Latency (rough, honest): show a stopwatch website in the shared window and photograph both
   screens together; the difference is the glass-to-glass delay. Repeat 3×, note the values.
   AKENO does not claim a latency number without this.

## Stage A2 — real PS5 picture (capture card or Remote Play)

Proves: real PS5 gameplay can be streamed through AKENO. Still **not** native PS5 capture.

- Capture card: PS5 → card (HDMI passthrough to the TV) → PC. On the PS5: Settings → System →
  HDMI → **Enable HDCP: off**. In AKENO: *Stream from: Capture card*, pick the card.
  - [ ] Picture, game sound, statistics. Note added latency vs the TV.
- Remote Play: start the PS Remote Play app on the PC, then *Stream from: A window or screen* →
  the Remote Play window only.

## Stage B — controller transport (real controller, no game control)

Proves: a real Windows controller reaches the host and the AKENO console app, with
authorization and revocation. **It does not control the game** (see stage D).

1. Guest PC: plug in an Xbox controller / DualSense / DualShock 4 (USB first, then Bluetooth),
   use Chrome or Edge.
2. Host streams (stage A/A2). Guest watches → **Request Player 2**.
   - [ ] Host sees "<name> wants to join as Player 2." → **Allow** → "Player 2 control granted to <name>."
3. Guest: **Use my controller** → press a button on the controller if the list is empty.
   - [ ] Guest's controller view lights the buttons; sticks and triggers move smoothly.
   - [ ] Host's monitor shows the same buttons/sticks; packets/s ≈ 60 while moving.
   - [ ] PS/Xbox button and Create/Share/View never light up on the host.
   - [ ] Releasing everything shows neutral on the host at once.
4. Safety:
   - [ ] Guest clicks into another window (AKENO loses focus) while holding a button → host shows
     neutral immediately ("lost focus").
   - [ ] Guest unplugs the controller while holding a button → host neutral.
   - [ ] Guest closes the tab while holding a button → host neutral within 0.5 s; Player 2 ends
     after ~10 s.
   - [ ] Host **Revoke Player 2** → guest's sending stops, host neutral, guest must ask again.
   - [ ] Guest leaves the party / host stops the stream → Player 2 ends.
5. PS5 diagnostic receiver (needs stage C's ELF approval below): with the host's console app
   running and paired, open its page → **SharePlay** tab (input is only sent to the console while
   this tab is open — it registers the console as a receiver):
   - [ ] "Player 2 diagnostic receiver" shows the guest's buttons and sticks live.
   - [ ] Stop moving: after 0.5 s it shows "everything released (neutral)".
   - [ ] Revoke on the website → the tab shows "The host revoked Player 2."
   - [ ] The game being played is **not** affected in any way (expected: it is not).

## Stage C — PS5 console app build with the SharePlay tab

Files: from the **PS5 console build** workflow run for the commit you test (GitHub → Actions →
"PS5 console build" → artifact `akeno-network-ps5-…`): `akeno-network-ps5.elf`, `SHA256SUMS`,
`BUILDINFO.txt`. Check the SHA-256 against `SHA256SUMS` before sending it to the console.

This build adds only display code (no new system imports; the import gate lists the same 96
imports as Alpha 0.4). Keep the previous ELF as fallback.

- [ ] The app starts as before; notifications, game detection, pairing, messages unchanged.
- [ ] A **SharePlay** tab exists; it says that this console cannot stream its own games and
  that Player 2 input controls nothing.
- [ ] With a SharePlay session in your party it shows who is streaming and who is Player 2.

**Native PS5 capture cannot be tested:** no capture mechanism exists to test
(`SHAREPLAY_PS5_RESEARCH.md` §2). If one is found later, it gets its own experimental ELF and
its own plan (API availability, frame access while a game runs, performance, cleanup).

## Stage D — true second player — **BLOCKED**

Requires a verified, safe virtual-controller (or hardware) mechanism, which does not exist in
this branch (`SHAREPLAY_PS5_RESEARCH.md` §3). Do not attempt it with third-party payloads that
inject into system processes. When unblocked, the test needs a game with real local two-player
support and must confirm: Player 1 and Player 2 move independently, no input interference,
stable control, and that disconnecting the guest returns everything to neutral.

## What to send back

For each stage: the checklist with ticks/crosses, screenshots of the SharePlay section with the
statistics line, the Stage A latency photos, the browser/OS versions and, if something failed,
the browser console log (F12 → Console → save) and `docker compose logs app livekit` from the
test instance.
