# Party voice chat (Alpha 0.4)

Voice chat for AKENO parties, in desktop and phone browsers. It reuses the existing party system
(`apps/server/src/modules/parties/`): a party's members can join the party's voice room, and
nobody else can. Audio is relayed by a self-hosted [LiveKit](https://livekit.io) server (SFU) and
is **never recorded, stored, transcribed or analysed**.

Status, as of 2026-10-14: implemented and tested against a real LiveKit server (see
[Testing](#testing)). **Not deployed.** Voice stays off until an operator configures it and
opens the firewall ports listed below. PS5 consoles join the same rooms through the AKENO voice
gateway (AKENO Native 1.1, [PS5_VOICE.md](PS5_VOICE.md); not yet run on a PS5).

## Choosing the media server

Party voice needs an SFU (selective forwarding unit): every participant sends one Opus stream
and the server forwards it to the others. Mixing on the server (MCU) costs far more CPU and
adds latency. Three established open-source options were compared:

| | **LiveKit** (chosen) | mediasoup | Janus |
| --- | --- | --- | --- |
| Licence | Apache-2.0 | ISC | GPL-3.0 |
| What it is | Complete server (Go, one binary or container) with signalling, room API, webhooks, JWT access tokens | Node.js library with a C++ media worker; you write signalling, rooms and auth yourself | C server with plugins (VideoRoom SFU, AudioBridge mixer) |
| Browser client | `livekit-client` (Apache-2.0): reconnects, device switching, speaking detection, connection quality | `mediasoup-client`: transport primitives only | `janus.js`: basic |
| Authorization model | Server-issued JWT per room and identity, with publish restrictions (for example microphone only); server API to remove participants and delete rooms | Whatever you build | Plugin tokens / admin API |
| NAT traversal | ICE over UDP and TCP; TURN server built in (optional) | ICE; needs a separate TURN server | ICE; needs a separate TURN server |
| Operations | One container, YAML config, Prometheus metrics | Custom process in our app (crash isolation, scaling: ours) | Separate daemon, plugin configuration |
| Fit for AKENO | Small team, single VPS, strict control over who joins | More code to write and secure | GPL obligations if modified and distributed; AudioBridge mixing is CPU heavy |

**Decision: LiveKit server v1.12.0** (`livekit/livekit-server:v1.12.0`, pinned) with
`livekit-client` 2.22.3 in the web app and `livekit-server-sdk` 2.19.1 in the API. It gives the
needed controls (short-lived tokens bound to a room, microphone-only publishing, server-side
removal, signed webhooks) without writing media code, and runs as one container next to the
existing stack. Egress (recording) and ingress are separate LiveKit services and are **not**
deployed.

## Architecture

```
 Browser (party member)                           VPS
 ─────────────────────                            ──────────────────────────────────────────
 1. POST /api/v1/parties/:id/voice/token  ──────▶ Caddy :443 ──▶ app:3000  (checks membership,
    ◀── { url, token (JWT, 120 s), room }                                    blocks, capacity)
 2. WebSocket wss://<domain>/livekit/rtc  ──────▶ Caddy :443 ──▶ livekit:7880 (signalling)
 3. Opus audio (WebRTC, DTLS-SRTP)        ◀─────▶ livekit:7882/udp  (or 7881/tcp fallback)
                                                    │  signed webhooks: joined / left / room ended
                                                    ▼
                                                  app:3000/api/v1/voice/webhook
                                          app ───▶ livekit:7880 room API (create room, remove
                                                    participant, delete room, list) — Twirp, JWT
```

- **One room per party**, named `akeno-party-<party id>`. LiveKit runs with
  `room.auto_create: false`, so a room exists only when the AKENO server creates it while
  issuing a token. Room capacity is the party's member limit.
- **Identity = AKENO user ID.** The token carries the display name and avatar (shown to the
  other members, who can see them anyway). One voice connection per user: joining from a second
  tab or device moves the connection there (the first one is told so and does not reconnect).
- **Audio never touches the AKENO app server.** The app only decides who may be in which room.
- The participant list is kept in memory by the API (like presence) and rebuilt from LiveKit
  every 30 seconds. Party members see who is connected (`member.voice.connected`); non-members
  see nothing about the voice room.

Code: `apps/server/src/modules/voice/` (service, routes), `apps/web/src/features/voice/`
(controller, provider, UI, microphone processing), `apps/web/src/features/settings/VoiceSettings.tsx`.

## Who can be in a room, and how access ends

| Check | When |
| --- | --- |
| Signed in with a website session (paired devices cannot get voice tokens) | Token request |
| Member of that party | Token request, join (webhook), every 30 s (reconciliation) |
| No block in either direction with anyone already in the room | Token request, join, reconciliation |
| Account active (not suspended) | Join, reconciliation |
| The browser session (or paired console) that requested the token of the connection in the room is still valid | Join, reconciliation (also after a restart: the credential is sealed, encrypted, into the token; a connection without a valid seal is removed) |
| Room not full (party member limit) | LiveKit, at join |

Access ends **at once** (the API removes the participant from the LiveKit room) when the user:
leaves the party; is removed by the leader; the party closes or empties (the room is deleted);
blocks or is blocked by someone in the room (the blocked user is disconnected); is suspended;
deletes their account; or signs out the browser session that requested the token (signing out,
"sign out other sessions", revoking that session, changing the password). This applies to the
connection actually in the room: a newer token that another session or console requested but
never used changes nothing, and after a restart the API asks LiveKit which credential the
connected participant holds. Anything the voice
server missed (for example during an outage) is corrected by the next reconciliation pass.

Tokens:

- Lifetime `VOICE_TOKEN_TTL_SECONDS` (default 120 s, range 30–600). The token is only needed to
  connect; reconnecting after a network problem asks the AKENO server for a new one, so a
  removed member cannot come back. **Measured:** LiveKit 1.12 still accepts a token for about
  60 s after its expiry (clock-skew leeway of its JWT validation: accepted 58 s, refused 61 s
  after a 1 s token expired), so a token can be used for up to TTL + 60 s. Expiry is therefore
  not what keeps removed users out: every join is checked again through the signed webhook,
  and a join that is not allowed is removed immediately.
- Grants: `roomJoin` for that one room, `canSubscribe`, `canPublish` limited to the
  **microphone** source; no data channel, no metadata changes, no screen or camera.
- Requests are rate limited (20 per minute per user).

Webhooks from LiveKit are verified (JWT signed with the API secret, including the body's
SHA-256) before anything is read; unsigned or altered requests get `401`. The webhook path is
excluded from the browser Origin check because LiveKit calls it server to server.

### Privacy

- No recording: no egress service, no recording code, no audio passes through the AKENO API.
- Voice settings (devices, volumes, mode, push-to-talk key, per-person volume and "mute for
  me") stay in the browser's localStorage; nothing about them is sent to the server.
- The database stores nothing about voice. LiveKit logs joins and leaves with user IDs at
  `info` level (container log, rotated like the other services).
- Muting someone "for me" unsubscribes from their audio: it stops reaching you at all and
  nobody else is told.

## Using voice (web)

- **Party page:** *Join voice* (the browser asks for the microphone the first time), mute,
  deafen (hear nobody, microphone muted), leave, connection quality, speaking indicators (green
  ring and "Speaking", from LiveKit's audio level detection), member mute state, per-person
  volume and "mute for me", a hold-to-talk button in push-to-talk mode.
- **Invitations:** party invitations offer *Accept* and *Join voice* (accept and connect in one
  tap) when voice is set up.
- **Every page:** a voice bar (sidebar on desktop, above the bottom navigation on phones) keeps
  the connection while you browse, with mute and leave.
- **Settings → Voice & Audio:** input and output device (output selection where the browser
  supports it), input volume (0–200 %), output volume (0–100 %), open microphone / voice
  activity (with a sensitivity slider shown on the level meter) / push-to-talk (key capture),
  echo cancellation, noise suppression, automatic gain control, a live microphone test with
  optional "hear yourself", a test sound, disconnect.
- **Reconnection:** short network drops are handled by LiveKit; if the connection is lost, the
  client reconnects with a fresh token (exponential backoff, up to 8 attempts) and says so.
  Being removed, the party ending, or moving to another tab are shown as such and do not
  reconnect.
- **Microphone release:** leaving voice, leaving the party, signing out or closing the tab stops
  the microphone (the browser's recording indicator goes off).

Microphone processing runs in an AudioWorklet (`gate-worklet.js`): input gain and the voice
activity / push-to-talk gate. If it cannot load (old browser, the file cannot be fetched), voice
still connects with the plain microphone and says so; push-to-talk then mutes and unmutes the
microphone instead. Remote audio plays through `<audio>` elements rather than Web
Audio because Chrome's echo canceller only hears audio played that way. Per-person volume is
therefore capped at 100 %.

### Browser and device notes (honest limits)

| Platform | Notes |
| --- | --- |
| Desktop Chrome / Edge | Tested (Chromium in automated tests). |
| Desktop Firefox, Safari | Expected to work (LiveKit supports them); **not tested** in this release. Firefox and Safari cannot choose the output device. |
| Android (Chrome) | Layout tested at phone size in Chromium; **not tested on a real phone**. Audio continues while the screen is on; Android may stop the microphone when the browser goes to the background or the screen locks. |
| iPhone / iPad (Safari and every iOS browser) | **Not tested on a real device.** iOS stops microphone capture when the browser is in the background or the screen locks; audio playback may need a tap ("Tap to hear the party"); websites cannot change playback volume, so per-person volume is replaced by "mute for me" (the UI says so). |
| PS5 browser | No microphone API in the console's browser; voice is not offered there. |

- Browsers require HTTPS (or `localhost`) for the microphone; the client says so otherwise.
- Push-to-talk keys work only while the AKENO tab has focus (browsers do not allow global
  hotkeys). The on-screen hold-to-talk button works on touch screens.
- After a phone call takes the microphone, the client offers "Resume microphone".

## Deploying the voice server

Nothing here is applied automatically. The production upgrade, the firewall change and the DNS
remain the operator's decisions.

**Inspect the VPS first.** This development environment cannot reach the production server, so
its CPU, memory, disk, bandwidth allowance, network and running services were not inspected.
Before enabling voice, run on the VPS and compare with the estimates below:

```sh
nproc; free -h; df -h /                    # CPU, memory, disk
docker compose --profile https ps          # existing services
docker stats --no-stream                   # current usage
ip -4 addr; curl -4 -s https://ifconfig.me # interfaces and public IPv4 (LIVEKIT_NODE_IP)
sudo ufw status verbose                    # host firewall (plus the provider's firewall panel)
sudo ss -tulpn | grep -E ':(7880|7881|7882)\b' || echo "voice ports free"
```

Also check the provider's monthly traffic allowance against the bandwidth section.

1. **Generate keys** (letters and digits; the secret at least 32 characters):
   ```sh
   echo "LIVEKIT_API_KEY=akeno$(openssl rand -hex 4)"
   echo "LIVEKIT_API_SECRET=$(openssl rand -hex 32)"
   ```
2. **Configure `.env`** (see `.env.example`, "Party voice chat"):
   ```sh
   LIVEKIT_URL=wss://<AKENO_DOMAIN>/livekit
   LIVEKIT_API_KEY=…
   LIVEKIT_API_SECRET=…
   LIVEKIT_NODE_IP=<public IPv4 of the VPS>   # or leave empty to discover it over STUN
   ```
   The app reaches LiveKit's API inside Docker at `http://livekit:7880` (compose default).
3. **Open the media ports** in every firewall in front of the server (VPS provider firewall and
   the host firewall such as `ufw`), inbound, from anywhere:

   | Port | Protocol | Purpose |
   | --- | --- | --- |
   | 7882 | UDP | WebRTC media (preferred path) |
   | 7881 | TCP | WebRTC media over TCP when UDP is blocked |
   | 443 | TCP | Already open for Caddy; carries signalling at `/livekit` |

   Port 7880 (signalling and API) is **not** exposed publicly; it is bound to `127.0.0.1` on
   the host and reached by Caddy and the app over the Docker network. Example host commands
   (for the operator to run after review; not run by this project):
   `ufw allow 7881/tcp && ufw allow 7882/udp`.
4. **Start it** together with the rest of the stack:
   ```sh
   docker compose --profile https --profile voice up -d
   ```
   Caddy (`infrastructure/caddy/Caddyfile`) forwards `/livekit/*` to `livekit:7880` with the
   prefix removed. LiveKit sends webhooks to `http://app:3000/api/v1/voice/webhook`.
5. **Check:** `GET /api/v1/voice/status` returns `{"enabled":true}`; a party shows *Join voice*;
   two people in a party hear each other. `docker compose logs livekit` shows the joins.

Without `--profile voice` the LiveKit container does not run; with `LIVEKIT_URL` set but
LiveKit down, token requests answer `503 VOICE_UNAVAILABLE` and everything else (parties,
party chat) keeps working.

Hardening of the LiveKit container in `compose.yaml`: non-root user, read-only filesystem,
all capabilities dropped, `no-new-privileges`, 512 MB memory limit, pinned image, its own key
pair, rooms only created by the API.

### NAT, TURN and Docker networking

- The browser connects directly to the VPS's public IP on 7882/udp (ICE "host" candidate,
  advertised from `LIVEKIT_NODE_IP` or discovered with STUN). The Docker port mapping must keep
  the same port numbers (it does: `7881:7881`, `7882:7882`). If the provider uses 1:1 NAT,
  set `LIVEKIT_NODE_IP` to the public address.
- When UDP is blocked, clients fall back to TCP 7881. Networks that only allow HTTPS on 443
  (some corporate or school networks) need TURN over TLS on 443. LiveKit has a TURN server
  built in, but port 443 is taken by Caddy, so it needs a second IP address or a separate
  domain with Caddy's layer-4 forwarding. **TURN is not enabled in this release** and has not
  been tested; gaming households rarely need it. When it is enabled, use LiveKit's built-in
  TURN (LiveKit gives TURN credentials only to participants who joined with a valid token, so it
  is not an open relay) rather than a standalone coturn with static credentials.
- Host networking (`network_mode: host`) is LiveKit's recommendation for large deployments; the
  bridge mapping above is enough for party sizes up to 16 and keeps the container isolated.

### Resource and bandwidth estimates

Measured on 2026-10-14 with the compose `voice` profile (production app image + LiveKit
container) on the development container (4 vCPU), Chromium participants using the fake
microphone, which sends a continuous tone, so **everyone was "talking" at once** (worst case):

| Participants in one room | LiveKit CPU | LiveKit memory | Upload per participant | Download per participant |
| --- | --- | --- | --- | --- |
| 0 (idle) | 0.2 % of a core | 13 MB | — | — |
| 6 | 8 % of a core | 66 MB | 32 kbit/s | 150 kbit/s |
| 8 | 12 % of a core | 74 MB | 32 kbit/s | 209 kbit/s |

Rules of thumb (Opus with DTX and RED, mono):

- Each participant uploads about 30–35 kbit/s while speaking and almost nothing while silent
  (DTX), and downloads about 30 kbit/s per other person speaking.
- Server traffic for a party of *n* where everyone speaks: in ≈ n × 32 kbit/s, out ≈
  n × (n − 1) × 30 kbit/s. A full 8-person party is ~0.26 Mbit/s in and ~1.7 Mbit/s out; in
  real conversation (one or two speaking at a time) out is roughly n × 60 kbit/s.
- Monthly transfer: one 8-person party talking 3 hours every evening ≈ 70 GB out in the worst
  case, typically well under 20 GB.
- A 2-vCPU / 2 GB VPS has room for several simultaneous parties; LiveKit's 512 MB limit and
  the measured ~10 MB per participant leave headroom for ~40 participants.

## Testing

| What | Where | Result (2026-10-14) |
| --- | --- | --- |
| Token rules, refusals (non-member, no CSRF, paired device), signed webhooks (forged and altered bodies rejected), member-only visibility, intruder removal, kick/leave/close, blocks, suspension, session sign-out, revocation of the connected credential (unused newer tokens, backend restart, fail closed), reconciliation, outage isolation, unconfigured voice, configuration validation, CSP | `apps/server/test/voice.test.ts` (fake LiveKit room API) | 25 passed |
| Preferences sanitizing, threshold/meter scale, microphone error messages | `apps/web/tests/unit/voice.test.ts` | 7 passed |
| Real LiveKit 1.12.0 + two (then three) Chromium browsers with fake microphones: join, **received audio with energy** (WebRTC stats), speaking indicator, mute state, mute-for-me stops receiving, voice survives navigation, reload leaves cleanly, microphone test level, second tab takes over, kicked member loses voice and is refused a new token, phone layout + join from an invitation, leaving releases the microphone, no console or CSP errors | `apps/web/tests/e2e/voice.spec.ts` | passed, also within the full E2E suite; skipped when `E2E_LIVEKIT_URL` is not set |
| Production image + compose `voice` profile (LiveKit as non-root, read-only): app reaches LiveKit API, webhooks arrive, two browsers exchange audio over UDP 7882 | manual script (see TESTING.md) | passed |
| Load: 6 and 8 participants | manual script | numbers above |
| LiveKit restart while two users talk | manual script | both clients showed *Reconnecting…*, rejoined with fresh tokens (the old room was gone) and exchanged audio again ~4 s after the restart |
| Repeated joins (5 cycles, plus 3 in the E2E) | manual script, `voice.spec.ts` | no live microphone tracks or open peer connections after each leave |
| Token expiry | manual script against LiveKit | see the leeway note under [Who can be in a room](#who-can-be-in-a-room-and-how-access-ends) |

Not tested: real phones (Android, iOS), Firefox and Safari, TURN, the production network and
firewall, more than 8 participants. **At least one real two-device test (for example a PC and
a phone on different networks) is still required before calling voice fully verified**; the
automated tests use Chromium's fake microphone on one machine.

## Future: screen sharing (not implemented)

The design leaves room for it without changing the security model:

- Tokens already list the allowed sources; screen sharing would add `SCREEN_SHARE` (and
  `SCREEN_SHARE_AUDIO`) only when the party allows it (a new party setting controlled by the
  leader), so the server stays the only authority.
- Video changes the resource picture completely (hundreds of kbit/s to several Mbit/s per
  viewer); it would need simulcast, viewer limits and new measurements before enabling.
- The client would use `getDisplayMedia` on desktop browsers only (phones and the PS5 browser
  cannot share their screen from a website).
