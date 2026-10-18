# Two-way party voice in AKENO NETWORK Native (PS5)

Status (2026-10-18): **implemented and tested on PCs end to end** (real AKENO server, real LiveKit
1.12 server, the voice gateway, AKENO Native with a simulated microphone, a LiveKit participant in
the role of a website member). **On a PS5 (12.20, build `196ca13c71d1-ps5`, owner report
2026-10-18):** the microphone works locally: `libSceAudioIn.sprx` loads, the port opens at
16000 Hz with 256-frame reads, the Settings microphone test's level follows speech, and the
microphone closes again ([PS5_NATIVE_1.1_HARDWARE_RESULTS_FW12.20.md](PS5_NATIVE_1.1_HARDWARE_RESULTS_FW12.20.md)). The console's audio *output* path is the one Native 0.1 proved
audible on 12.20. **Two-way network voice has not run on a PS5**: the production server answered
the voice pass request with 404 because the device voice pass and the gateway are not deployed.
The hardware test is
[HARDWARE_TEST_PLAN_PS5_NATIVE_1.1.0-beta.1.md](HARDWARE_TEST_PLAN_PS5_NATIVE_1.1.0-beta.1.md).

Goal: open AKENO Native on the PS5, select the party, press **Join voice**, hear the friends who
are in the party's voice chat (website, desktop or phone) and talk back with the DualSense or a
headset microphone. The PS5 user is a normal participant of the same LiveKit room; the others see
and hear them exactly like a browser user.

## Transport: native WebRTC on the console, or an Opus gateway?

Party voice runs on a self-hosted LiveKit SFU ([VOICE.md](VOICE.md)). Browsers speak WebRTC to it.
Two ways were compared for the console:

| | A: WebRTC client in AKENO Native | B: **AKENO voice gateway** (chosen) |
| --- | --- | --- |
| What runs on the PS5 | ICE (STUN), DTLS-SRTP (mbedTLS), SRTP, RTP/RTCP, SDP, LiveKit's protobuf signalling, Opus | TLS WebSocket (the client library AKENO Native already uses), Opus |
| What runs on the server | nothing new | `apps/voice-gateway`: one LiveKit participant per console (LiveKit's official Node SDK, prebuilt libwebrtc), Opus to/from the console, mixing |
| Network from the console | UDP 7882 (or TCP 7881) to LiveKit, NAT traversal | one TLS connection to `wss://<domain>/voice-gateway` (port 443, through Caddy) |
| Latency | lowest (direct to the SFU) | one more hop and a mix: adds about one 20 ms frame; gateway round trip measured 0–37 ms on a PC (loopback), real networks add their own |
| Server load | none extra | Opus decode + encode per console: ~0.2 ms CPU per 20 ms frame (≈1 % of a core per console, measured) |
| Authentication | a LiveKit token on the console | the same LiveKit token (from the AKENO server, device-authorized), verified by the gateway and again by LiveKit |
| Security surface on the console | a new DTLS/SRTP/ICE stack written from scratch in C | none new: TLS and WebSocket code already in use, plus Opus |
| Maintainability | AKENO maintains a WebRTC implementation against LiveKit's evolving signalling | LiveKit maintains the WebRTC side (official SDK); AKENO maintains a small protocol |
| Risk | very high: interop bugs only visible against real servers; mbedTLS DTLS-SRTP would need a separate configuration (the daemon's must stay unchanged) | moderate: a new service to deploy |
| Can be tested here | partly | **fully** (CI: real LiveKit, gateway, both directions) |

**Decision: B.** It is the solution that can be built, secured and tested to the end now; A stays
possible later behind the same UI if the gateway's extra hop ever matters.

## Architecture

```
 PS5: AKENO Native (foreground, websrv launcher)
   capture  ── libSceAudioIn (DualSense / headset mic, 16 kHz S16 mono, 256 frames) ──┐
             48 kHz (x3 polyphase) · mic volume · Opus 32 kbit/s, 20 ms, FEC          │
   network  ── voice pass: POST /api/v1/parties/:id/voice/device-token ─▶ daemon (IPC 3) ─▶ AKENO server
             TLS WebSocket wss://<server host>/voice-gateway  ◀──────────────────────────┘ (Opus both ways)
   playback ◀─ jitter buffer (60–200 ms) · Opus decode / concealment · volume · sceAudioOut (48 kHz stereo)

 AKENO server (VPS, compose voice profile)
   app        issues the voice pass (device-authorized), removes participants (party left, blocks, unpaired …)
   voice-gateway  verifies the pass, joins LiveKit as the user, decodes the console's mic into a
                  microphone track, mixes the others into one Opus stream for the console
   livekit    the existing SFU; browsers and apps connect to it directly (unchanged)
```

## The voice pass (server)

`POST /api/v1/parties/:id/voice/device-token`, only for paired devices (bearer token), with the
existing **`parties:write`** permission (the frozen release daemon cannot ask for new scopes; a
console without that permission gets `403 DEVICE_SCOPE_DENIED`). The daemon passes it through its
native write list (**native interface 3**, native-IPC build only; the release daemon is unchanged).

| Rule | How |
| --- | --- |
| Same checks as browsers | member of the party, no block with anyone in the room, room capacity = party limit |
| Short-lived | `VOICE_TOKEN_TTL_SECONDS` (default 120 s); only needed to join; reconnects ask again |
| Bound | one room (`akeno-party-<id>`), identity = the user ID, microphone-only, no data, no metadata changes |
| Marked for the gateway | participant attribute `akeno.client = console-gateway` (no device ID in it: other participants can read attributes) |
| Device-authorized | the server remembers which device asked; joins and the 30 s reconciliation check that the device still exists, is not expired and holds `parties:write` |
| Revocation | unpairing the console, removing `parties:write`, a password change, leaving the party, being removed, blocks, suspension: the user is removed from the room at once (the gateway closes the console with 4003) |
| Never | website cookies or sessions on the console; the device token never leaves the daemon |
| Off | `VOICE_GATEWAY_URL` empty → `503 VOICE_GATEWAY_UNAVAILABLE`; browsers unaffected |

## Gateway protocol

`wss://<AKENO server host>/voice-gateway` (AKENO Native refuses a gateway on another host and
anything that is not `wss://`), TLS verified with the daemon's CA bundle or `/data/akeno/ca.pem`.

| Direction | Message |
| --- | --- |
| console → gateway, first, within 5 s | `{"type":"hello","protocol":1,"token":"<voice pass>","client":"akeno-native/1.1.0-beta.1","muted":false,"deafened":false}` |
| gateway → console | `{"type":"ready","protocol":1,"room":…,"identity":…,"sampleRate":48000,"channels":1,"frameMs":20}` |
| console → gateway | `{"type":"mute","muted":b}`, `{"type":"deafen","deafened":b}`, `{"type":"ping","id":n}`, `{"type":"leave"}` |
| gateway → console | `{"type":"participants","list":[{"id","name","muted","speaking","level","self"}]}` (on change, levels at most every 200 ms), `{"type":"state","state":"reconnecting|connected"}`, `{"type":"error","code","message"}`, `{"type":"pong","id"}` |
| both, binary | `0x01`, sequence number (uint16 big endian), one 20 ms Opus packet (48 kHz mono, ≤ 400 bytes) |
| close codes | 4001 pass refused, 4003 removed, 4004 joined elsewhere, 4005 room ended, 4006 protocol, 4007 gateway full, 4008 idle, 4009 voice server unavailable, 1000 left |

Gateway limits: 4 KB messages, ≤ 75 audio frames/s up, 500 ms capture queue, 64 KB send buffer
(frames are dropped, not queued, for a slow console), 30 s idle, one session per account (a newer
one replaces the older; LiveKit does the same), `GATEWAY_MAX_SESSIONS` (default 50), requests with
an `Origin` header (browsers) refused. Mute unpublishes the console's microphone track (others see
it muted); deafen stops audio to the console (and AKENO Native also mutes).

## On the console

| Part | Details |
| --- | --- |
| Join | Party screen → **Join voice** (only when the server offers party voice) → confirmation "Join voice with your microphone?" → only then the microphone opens |
| Controls (rows under Join/Leave) | Mute/Unmute microphone (level shown), Deafen/Undeafen, Voice volume and Microphone volume (Left/Right, 0–200 %), Leave voice |
| Indicators | members marked **speaking** (green) from the gateway's levels; the VOICE panel lists who is in voice, muted, speaking; a "Voice" chip in the top bar |
| Microphone | `libSceAudioIn.sprx` loaded with `dlopen` on first use; `sceAudioInOpen(foreground user, 1 "general", 0, 256, 16000, 0 = S16 mono)` — the declaration of the PS5-Audio-Passthrough-Research sample (DualSense capture, foreground app, PS5 Pro 12.70); 48 kHz tried if 16 kHz is refused. The system decides whether the DualSense or a headset microphone is used (the foreground user's device). Not linked, so a firmware without it cannot stop AKENO Native from starting; the error code is shown and reported |
| Output | `sceAudioOut` main port, 48 kHz S16 stereo, 1024 frames: the port and format Native 0.1 proved audible on 12.20. TV or headset: wherever the system routes the main output |
| Jitter and loss | reorder, duplicates, late packets; 60 ms prefill, 200 ms bound (oldest dropped when the sender's clock runs faster); a missing packet is concealed by Opus; five losses in a row → silence and rebuffer; the encoder sends in-band FEC for 10 % loss |
| Reconnect | after a lost connection: a new pass and connection after 1, 2, 4, 8, 10 s (six tries); not after removal, replacement, room end or a refused pass |
| Cleanup | Leave voice, leaving or losing the party, removal, a failure, Exit: the microphone port closes, the socket closes, the output port closes |
| Microphone test | Settings → **Microphone test** (asks first): live level, peak, rate; Square plays the microphone back to the user (headset recommended); nothing is sent, recorded or stored |
| Privacy | nothing is recorded or stored; the log and report contain states, codes and counts only (`voice.stats`), never names, audio or the voice pass |
| Safety gate | `console/check-native.sh`: still the same 51 reviewed system imports; run-time loading only in the platform layer, only `libSceAudioIn.sprx` with `sceAudioInOpen/Input/Close` |

## Operating the gateway (owner; nothing is deployed by this work)

> **Separate server deployment required.** The device voice pass (AKENO server) and the voice
> gateway are new server-side code. Neither runs anywhere yet; without both, Join voice on the PS5
> cannot work. Review and plan: [DEPLOYMENT_REVIEW_PS5_VOICE.md](DEPLOYMENT_REVIEW_PS5_VOICE.md).


1. `compose.yaml` voice profile now contains `voice-gateway` (image from
   `infrastructure/docker/voice-gateway.Dockerfile`, Debian-based because LiveKit's Node SDK ships
   glibc builds only); Caddy routes `/voice-gateway` to it.
2. In `.env`: `VOICE_GATEWAY_URL=wss://<AKENO_DOMAIN>/voice-gateway` (and voice itself configured
   as in [VOICE.md](VOICE.md)). `VOICE_GATEWAY_MAX_SESSIONS` optional.
3. `docker compose --profile voice up -d --build`, then check `GET http://voice-gateway:7890/healthz`
   from inside the network. No new firewall port: consoles use 443.
4. The console needs the **"parties"** permission (granted at pairing; check on the website's device
   page).

Test this on disposable infrastructure first (as CI does); production was not changed.

## Testing (what is evidence of what)

| Level | What | Where | Result |
| --- | --- | --- | --- |
| Unit (PC, sanitizers) | Opus round trip (tone survives, PLC), jitter buffer (reorder, duplicates, late, loss, bounds, wrap), 16→48 kHz, gain/levels/meters, protocol and credential parsing (wss only, same host), session model, party rows, controls | `clients/ps5-native/tests/unit` | pass (1,521 checks in total) |
| Server | device pass contents and limits, refusals, revocation by unpairing / scope / leaving, refused joins, 503 without gateway, configuration | `apps/server/test/voice.test.ts` | pass |
| Gateway with real LiveKit | console client ↔ LiveKit participant: tones both ways, mute/unmute, deafen, ping, removal 4003, refused passes, browsers, replacement | `apps/voice-gateway/test` | pass |
| End to end | real server + LiveKit + gateway behind TLS + daemon (IPC 3) + AKENO Native (simulated microphone 300 Hz) + a website member through LiveKit (600 Hz): consent before the microphone, both directions by frequency, mute, unmute, deafen, volume, dropped connection reconnects, removing "parties:write" ends voice (4003), no token in logs, one realtime session | `clients/ps5-native/tests/e2e/run.mjs --livekit` | pass |
| PS5 build | ELF with libopus, gate, reproducible | CI PS5 job | builds; gate OK |
| PS5 hardware | microphone (open, level, close) | the owner, 12.20, `196ca13c71d1-ps5` | **pass** ([PS5_NATIVE_1.1_HARDWARE_RESULTS_FW12.20.md](PS5_NATIVE_1.1_HARDWARE_RESULTS_FW12.20.md)) |
| PS5 hardware | playback, two people talking, latency, long sessions | the owner | **not done** (needs the server side deployed) |

## In-game voice (separate research, does not block this milestone)

AKENO Native's voice lives in its own foreground process. What is known today:

| Situation | Expectation | Evidence |
| --- | --- | --- |
| AKENO Native open (foreground) | voice works (if the microphone opens on 12.20) | to be tested |
| The user presses PS and starts or resumes a game | the system suspends the previous foreground app; voice most likely stops (connection drops, the gateway closes after 30 s idle) | PS5 app model; not tested |
| A background payload (the daemon) playing audio | accepted but silent on 12.20 | audio lab v2, test C |
| OnionHEN Toolbox plugin | a separate background process; output likely silent like the daemon; microphone unknown | not tested; OnionHEN not installed |

So voice while playing needs a process that is allowed audio while a game is in front: the system's
own party chat does this; homebrew has shown nothing comparable. Research steps (each needs its
own approval, none started): (1) measure what happens to AKENO Native's voice when a game is
resumed from the home screen (does the process keep running, is audio muted, does the microphone
keep delivering); (2) only with OnionHEN approved: whether a Toolbox plugin may play or capture
audio while a game runs. Claims about voice during games wait for that evidence.
