# SharePlay architecture (Alpha 0.5, experimental)

SharePlay is a capability of an existing AKENO party: one member streams gameplay (video + game
audio) to the other members, and may give one member **Player 2** controller access. It reuses
the party system, its authentication and authorization, and the LiveKit server already deployed
for voice. It is **off by default** (`SHAREPLAY_ENABLED=false`) and nothing in it is deployed.

What is real today and what is not is tracked in [SHAREPLAY_STATUS.md](SHAREPLAY_STATUS.md);
the PS5 findings are in [SHAREPLAY_PS5_RESEARCH.md](SHAREPLAY_PS5_RESEARCH.md). In short: the
stream and the controller transport work end to end in browsers; streaming **from inside the
PS5** and turning Player 2 input into **a real second controller on the PS5** are not possible
with any verified, safe mechanism, so those adapters report "unsupported".

## Overview

```
 Host's PC (Chrome/Edge)                        VPS                           Viewers (PC / phone)
 ───────────────────────                        ──────────────────────────    ────────────────────
 capture: window (PS Remote Play)  ──WebRTC──▶  LiveKit room                ──WebRTC──▶  <video> + game audio
          or HDMI capture card     (H.264,      akeno-shareplay-<party>         (simulcast layer chosen
                                    simulcast,  (separate from the voice        by player size/network)
                                    Opus stereo) room akeno-party-<party>)
        ▲  POST /parties/:id/shareplay/start|token|stop|quality        ▲  POST …/token (viewer)
        │  ─────────────────────────────▶ app:3000 (authorizes; never touches media)
        │                                   │  signed webhooks: joined / left / track published
        │                                   ◀───────────────────── LiveKit
        │
 Player 2 (guest PC, Gamepad API)
   wss …/api/v1/shareplay/input?role=sender  ──32-byte packets──▶ app:3000 InputRelay
                                                                   │ checks grant/epoch/seq/rate
                     host browser  ◀── role=monitor (JSON) ────────┤
                     host's paired AKENO console app ◀── realtime "shareplay.input" ─┘
                     (diagnostic receiver: displays, never injects)
```

## Why these choices

| Decision | Reason |
| --- | --- |
| Reuse LiveKit, **separate room** per party (`akeno-shareplay-<id>`) | Video load, failures and reconnects cannot affect the voice room; voice tokens stay exactly as in Alpha 0.4 (microphone only). One LiveKit server, one webhook URL. |
| Browser capture (window or capture card) | The only real PS5 picture available today (see research). A desktop share is labelled as such; it is not "PS5 capture". |
| H.264 with VP8 backup, simulcast low layer, `maintain-framerate`, `contentHint = motion` | Hardware H.264 encoders are common on Windows PCs; motion matters more than sharpness for games; slow viewers get a 360p/480p layer instead of stalling everyone. |
| Opus **stereo**, 128 kbit/s, no DTX/RED, no echo cancellation/noise suppression/AGC | Game audio is music-like and must not be treated as speech or noise. |
| Viewers: `adaptiveStream` | LiveKit sends the layer that fits the player; when no player is visible (viewer on another page) the video subscription pauses and only game audio continues. |
| Controller input over an **AKENO WebSocket relay**, not LiveKit data | The PS5 console app cannot join WebRTC, but already has an authenticated realtime link to the server. The server becomes the enforcement point for every packet (grant, epoch, ordering, rate), so LiveKit tokens never need `canPublishData`. Path latency is similar (the SFU runs on the same VPS); see "Latency". |
| In-memory session state, no migration | Like voice presence. A restart ends every session and every grant — the safe failure. |

## Server (`apps/server/src/modules/shareplay/`)

- `service.ts` — sessions (one per party), tokens, webhooks, reconciliation, Player 2 grants and
  every automatic revocation.
- `input-relay.ts` — the controller WebSocket (sender and monitor), validation and forwarding.
- `routes.ts` — REST and WebSocket routes. `capabilities.ts` — source/adapter statements.

### API (prefix `/api/v1`)

| Method & path | Who | Notes |
| --- | --- | --- |
| `GET /shareplay/status` | session or device (`parties:read`) | enabled, limits, qualities, sources, remote input, virtual controller (always unsupported) |
| `GET /parties/:id/shareplay` | member (session or device) | session state and what *you* may do |
| `POST /parties/:id/shareplay/start` `{source, quality?}` | member, website session | one stream per party; `ps5-native` → `409 SOURCE_UNSUPPORTED` |
| `POST /parties/:id/shareplay/stop` | host or party leader | ends for everyone |
| `POST /parties/:id/shareplay/quality` `{quality}` | host | within `SHAREPLAY_MAX_QUALITY` |
| `POST /parties/:id/shareplay/token` | member, website session | role decided by the server (host/viewer); 20/min |
| `POST /parties/:id/shareplay/controller/request` · `/cancel` | guest | needs a live stream and remote input enabled |
| `POST /parties/:id/shareplay/controller/approve` · `/deny` `{userId}` | host only | approval needs a pending request (60 s) |
| `POST /parties/:id/shareplay/controller/revoke` | host or Player 2 | immediate |
| `GET /shareplay/input?party=&role=sender&epoch=` (WebSocket) | the granted guest | binary packets in |
| `GET /shareplay/input?party=&role=monitor` (WebSocket) | the host | JSON relays out |
| `POST /parties/:id/shareplay/receiver` | paired console app of a member (device token) | opt in to `shareplay.input` for 90 s (the SharePlay tab renews every 30 s) |

Realtime events (all party members): `shareplay.changed {partyId, ended}`. To the host's paired
console apps that opted in as receivers only: `shareplay.input` (see protocol) — older console
apps never opt in, so they are never flooded with input events.

### Permissions (least privilege)

| Permission | Voice (unchanged) | SharePlay host | SharePlay viewer | Player 2 guest |
| --- | --- | --- | --- | --- |
| Room | `akeno-party-<id>` | `akeno-shareplay-<id>` | `akeno-shareplay-<id>` | (as viewer) |
| Publish | microphone | screen share + screen share audio | nothing | nothing |
| Subscribe | yes | **no** | yes | yes |
| Publish data | no | no | no | no |
| Controller input | — | — | — | only with an explicit, current grant, via the AKENO relay |

Voice participants gain nothing from SharePlay and vice versa; watching gives no controller
access; party membership alone gives no controller access.

### Player 2 grant lifecycle

1. Guest: **Request Player 2** (only while the stream is live; expires after 60 s).
2. Host: **Allow** → grant `{grantId (UUID), epoch (new number), guestId, slot 2, expiresAt}`
   (`SHAREPLAY_GRANT_MAX_MINUTES`, default 120). **Deny** drops the request.
3. Guest opens the controller link with that epoch; the server replies `{"type":"ready",
   "lastSeq":N}` and the guest continues numbering after N.
4. The grant ends — the server sends a **release** to every receiver and closes the guest's link
   with `4410` — when: the host revokes; Player 2 gives it back; it expires; the guest leaves or
   is removed from the party; the host leaves (the stream ends); the party closes; either blocks
   the other; the guest is suspended or deleted; the browser session that holds the link or the
   token is signed out or revoked; the host's LiveKit connection drops; the guest stops watching
   (LiveKit reports the leave); the guest's link stays closed for 10 s; the stream ends or its
   video is unpublished. A grant can never be re-used: approving needs a new request, and a link
   with an old epoch is refused (`409 GRANT_CHANGED`).

### Controller protocol v1

Defined once in `packages/shared/src/shareplay/controller-protocol.ts` (also implemented in C in
`clients/ps5/app/shareplay_rx.c`). 32-byte little-endian packets: magic `0xA7`, version 1, kind
(state/release), slot (2), grant epoch, sequence number, sender clock, button bitset (W3C
standard layout), four sticks as int16, two triggers as uint8, 2 reserved bytes (must be 0).

- **Strict decoding:** wrong size, magic, version, kind, slot, unknown button bits, −32768 on an
  axis, non-zero reserved bytes, a non-neutral release, and the **PS/Home and Create buttons**
  are refused (the sender also clears them).
- **Ordering:** a packet is applied only if its sequence number is higher than the last applied
  one of the same grant; stale or duplicated packets are dropped and counted.
- **Rate:** 120 packets/s sustained with a 30-packet burst per link; beyond that packets are
  dropped; floods (600 dropped) or malformed traffic (50 bad) close the link (`4400`).
- **Heartbeat & watchdog:** the sender repeats its state every **100 ms**; every receiver
  returns to neutral after **500 ms** without fresh input. Releases are applied immediately
  whatever their sequence number (going to neutral is always safe).
- **Backlog:** a receiver whose socket is backed up (> 16 KiB) skips state packets — never
  queued stale input — while releases are always sent.
- **Sender policy (browser):** only controllers with the browser's "standard" layout; radial
  stick dead zone 12 % with rescaling, trigger dead zone 4 %, clamping; a release on window blur,
  hidden tab, controller disconnect, polling stall (> 250 ms) or stop; the controller must be
  activated with a click and a button press (browser rule). While a controller is Player 2,
  AKENO's own gamepad UI navigation pauses.

## Web (`apps/web/src/features/shareplay/`)

- `stream-controller.ts` — capture, publish, watch, statistics, reconnection, cleanup.
- `SharePlayProvider.tsx` — one stream per signed-in user across pages; leaves when the party
  changes; reacts to `shareplay.changed`.
- `SharePlayPanel.tsx` — the party page section; `SharePlayBar.tsx` — capture indicator /
  watching bar on every page.
- `player-two.ts` — Gamepad sender and host monitor; `PlayerTwoPanel.tsx` — request/allow/deny/
  revoke and the controller view.

Privacy: capture starts only after **Start SharePlay**, a warning, and the browser's own picker;
the AKENO tab itself cannot be picked (`selfBrowserSurface: exclude`); a pulsing red indicator
and the sidebar bar show that you are sharing; stopping stops every captured track at once;
nothing is recorded (no egress), and the server never sees media.

Platforms: streaming needs a desktop browser with `getDisplayMedia`/`getUserMedia` (Chrome/Edge
on Windows first). Watching works in any browser with WebRTC (desktop and Android Chrome;
tested in Chromium). The PS5 browser is **not assumed** to play WebRTC video; the console page
shows status only.

## Console app (`clients/ps5/app/`)

`shareplay_rx.c` validates, orders and watchdogs relayed Player 2 states and shows them on the
local page (SharePlay tab, `/api/shareplay`). The virtual-pad and native-capture adapters exist
as functions that always return "unsupported" with the reason. No new system imports.

## Configuration

| Variable | Default | Meaning |
| --- | --- | --- |
| `SHAREPLAY_ENABLED` | `false` | Master switch (needs the `LIVEKIT_*` settings) |
| `SHAREPLAY_MAX_VIEWERS` | `4` | Viewers per stream (1–15); room capacity = viewers + 1 |
| `SHAREPLAY_MAX_QUALITY` | `720p60` | Highest preset a host may pick (`480p30` … `1080p60`) |
| `SHAREPLAY_REMOTE_INPUT_ENABLED` | `false` | Player 2 controller relay (needs `SHAREPLAY_ENABLED`) |
| `SHAREPLAY_GRANT_MAX_MINUTES` | `120` | Longest Player 2 grant (5–720) |

Token lifetime reuses `VOICE_TOKEN_TTL_SECONDS`. No new ports: SharePlay uses LiveKit's existing
7881/tcp, 7882/udp and the `/livekit` route. Compose passes the variables through.

## Quality presets

Encoder targets (caps, not promises; the source and congestion control decide):

| Preset | Main layer | Low simulcast layer |
| --- | --- | --- |
| 480p30 | 854×480, 30 fps, ≤ 1.2 Mbit/s | — |
| **720p30 (default)** | 1280×720, 30 fps, ≤ 2.5 Mbit/s | 640×360, 30 fps, ≤ 0.6 Mbit/s |
| 720p60 | 1280×720, 60 fps, ≤ 4.0 Mbit/s | 640×360, 30 fps, ≤ 0.6 Mbit/s |
| 1080p30 | 1920×1080, 30 fps, ≤ 4.5 Mbit/s | 854×480, 30 fps, ≤ 1.0 Mbit/s |
| 1080p60 | 1920×1080, 60 fps, ≤ 6.5 Mbit/s | 854×480, 30 fps, ≤ 1.0 Mbit/s |

Plus game audio ≤ 128 kbit/s. Whether a capture card or the Remote Play window can deliver a
preset is **not known** until tested (stage A2).

## Bandwidth and the VPS

**Measured** (development container, see `docs/perf/shareplay-container-2026-10-16.json`): the
LiveKit process used **~5–6 % of one core and ~107 MB** while forwarding one stream to 1 and 3
viewers. The broadcaster's encoder in that container was CPU-starved (headless software
encoding: ~0.3–0.5 Mbit/s, very low frame rate, `qualityLimitationReason=bandwidth`), so its
bitrates are **not** representative of a real PC and are not used below.

**Estimates** from the preset caps (upper bounds, not measurements), 720p30:

| | Inbound to the VPS | Outbound from the VPS |
| --- | --- | --- |
| Per stream | ≤ 2.5 + 0.6 (low layer) + 0.13 audio ≈ **3.2 Mbit/s** | — |
| Per viewer | — | ≤ 2.6 Mbit/s (main layer) or ≤ 0.7 Mbit/s (low layer) |
| 1 stream, 4 viewers | ≈ 3.2 Mbit/s | ≤ ~10.5 Mbit/s |
| One evening (3 h), 1 stream, 2 viewers at full layer | ≈ 4.3 GB | ≈ 7 GB |

Compared with voice (≈ 0.26 Mbit/s in / 1.7 Mbit/s out for a full 8-person party), video is
one to two orders of magnitude more. Before enabling on the one.com VPS: check its monthly
traffic allowance and port speed (not inspected — no access from this environment), keep
`SHAREPLAY_MAX_VIEWERS` small and `SHAREPLAY_MAX_QUALITY` at `720p60` or lower, and run the
measurement script there with real browsers. No production bandwidth test was run.

## Latency

Not measured end to end (no real devices). Components: capture (window or card) + encoder +
uplink to the VPS + SFU + downlink + jitter buffer + decoder. The viewer shows the round-trip
time to the SFU and the jitter-buffer delay as measured by the browser; glass-to-glass latency
must be measured with the stopwatch method in stage A. Player 2 input travels guest → VPS → host
(console app or browser): one network crossing each way, comparable to the SFU path.

## Testing

| Layer | Where |
| --- | --- |
| Protocol, normalization, dead zones, codec, ordering, rate limit, watchdog | `packages/shared/src/shareplay/controller-protocol.test.ts` |
| Authorization, tokens, webhooks, grants, revocation, relay (real WebSockets, fake LiveKit API) | `apps/server/test/shareplay.test.ts` |
| Sender neutral policy, monitor ordering/watchdog | `apps/web/tests/unit/shareplay.test.ts` |
| Two browsers + real LiveKit: frames and colours decoded, audio energy, controls, voice coexistence, Player 2, navigation, quality change, stop, reload, phone | `apps/web/tests/e2e/shareplay.spec.ts` |
| Console receiver (C) and the server → console path | `clients/ps5/tests/unit/test_shareplay.c`, integration step in `clients/ps5/tests/integration/run.mjs` |
| Resource measurement | `infrastructure/scripts/shareplay-measure.mjs` |
