# AKENO LAN Play — architecture

LAN Play adds an authenticated "virtual LAN" capability to an existing AKENO party. This change
implements the **control plane**; the **data plane** (carrying game traffic) is a separate,
pluggable transport that is **not** provided here, so LAN Play reports "Transport not available".

Status of what is real is tracked below and in `LAN_PLAY_FEASIBILITY.md`. It is **off by default**
(`LAN_PLAY_ENABLED=false`) and independent of SharePlay and voice.

## Control plane vs data plane

```
        AKENO control plane (this change — apps/server)                 Data plane (NOT in this build)
        ───────────────────────────────────────────────                ──────────────────────────────
 Browser / console  ──HTTPS──▶  app:3000                                 AKENO LAN Bridge (companion
   party member                 /api/v1/lanplay/* and                    program on a nearby PC/Linux
                                 /api/v1/parties/:id/lanplay/*            box, or a verified engine)
                                   • create / end a session                     ▲
                                   • join / leave, roles                        │ implements
                                   • select game (compatibility DB)             │ LanTransportAdapter
                                   • authorize → issue expiring credential ─────┘ (transport.ts)
                                   • revoke on every membership change
                                 realtime: "lanplay.changed"              game LAN frames ◀───▶ game LAN frames
                                                                          (captured & forwarded by the
   The server NEVER sees or forwards game packets. It only decides         transport, authenticated with
   who may connect and hands out short-lived credentials.                  the credential AKENO issued)
```

Because no transport is wired, `authorize` returns "unavailable", no credential is issued, and
`POST …/lanplay/connect` answers `503 LAN_TRANSPORT_UNAVAILABLE`. The credential path itself is
implemented and tested (with a stub transport) so a real adapter drops in without control-plane
changes.

## Server module (`apps/server/src/modules/lanplay/`)

- `service.ts` — `LanPlayService`: one in-memory session per party; create/terminate, join/leave,
  host/player roles, membership checks, game selection, expiring connection credentials (SHA-256
  hashed, per-participant epoch), reconnection, and every automatic revocation. A restart ends all
  sessions — the safe failure, exactly like voice and SharePlay.
- `transport.ts` — `LanTransportAdapter` (the data-plane seam) and `UnavailableTransport` (the only
  adapter wired; authorizes nothing). This is the single place a future verified transport attaches.
- `capabilities.ts` — honest per-platform-pair capability statements.
- `routes.ts` — REST routes; nothing carries game traffic.

### API (prefix `/api/v1`)

| Method & path | Who | Notes |
| --- | --- | --- |
| `GET /lanplay/status` | session or device (`parties:read`) | enabled, transport availability, limits, honest platform pairs |
| `GET /lanplay/games` | session or device (`parties:read`) | compatibility database (facts + sources) |
| `GET /parties/:id/lanplay` | member (session or device) | session state and what *you* may do |
| `POST /parties/:id/lanplay` `{platform, gameId?}` | member, website session | creates the session; caller becomes host; one per party |
| `POST /parties/:id/lanplay/join` `{platform}` | member, website session | join the open session |
| `POST /parties/:id/lanplay/game` `{gameId\|null}` | host, website session | choose/clear the game |
| `POST /parties/:id/lanplay/connect` | participant, website session | asks the transport; `503 LAN_TRANSPORT_UNAVAILABLE` until one exists |
| `POST /parties/:id/lanplay/leave` | participant, website session | leave the session |
| `DELETE /parties/:id/lanplay` | host or party leader | ends it for everyone |

Reads are allowed for paired devices with `parties:read` (the console shows LAN Play read-only, like
SharePlay). Everything that creates, joins, ends or connects requires a signed-in browser session.

Realtime: `lanplay.changed {partyId, ended}` to all party members (refetch the party view).

### Roles and lifecycle

1. A party member **creates** a session and becomes the **host** (declares its platform, optionally a
   game from the compatibility DB).
2. Other members **join** as **players** (up to `LAN_PLAY_MAX_PARTICIPANTS`, default 8).
3. A participant calls **connect** to obtain a short-lived credential for the transport. With no
   transport, this is refused with the honest reason and no credential is minted.
4. The session **ends** — and every credential is revoked — when: the host ends it; the party leader
   ends it; the host leaves the party or signs out; the party closes; it passes its TTL. A single
   player is removed (and its credential revoked, its epoch bumped) when it leaves, is removed, is
   blocked by/against the host, is suspended/deleted, or signs out. These reuse the exact party,
   block, suspension and session-revocation hooks SharePlay uses.

## Transport adapter interface (the extension point)

`LanTransportAdapter` (see `transport.ts`): `availability()`, `authorize(session, participant)` →
connection descriptor or `null`, `revoke(sessionId, userId)`, `release(sessionId)`. A verified,
independently maintained engine (e.g. a Wintun/TAP + QUIC bridge, per `LAN_PLAY_FEASIBILITY.md`)
implements this and is injected into `LanPlayService`. AKENO reimplements **no** low-level packet
forwarding; the engine owns the data plane, AKENO owns identity, membership and revocation.

## The planned AKENO LAN Bridge (Track A — design only here)

A companion program on a nearby PC/Linux box (or a compatible router) that a future transport would
drive. Design goals, carried from the spec: pick the correct local interface; identify the intended
local game device by MAC; transport only that device's relevant LAN traffic (never the home
network's other traffic); support the discovery mechanism each verified game needs; preserve packet
order and tolerate loss; connect only to approved AKENO party members with the credential above;
show status, latency and counters; and disconnect immediately when the session ends. It must never
change the PS5's DNS, PSN routing or system settings, and must cleanly undo any local change on exit.
None of this bridge is implemented in this change.

## What this change deliberately does not do

- No packet capture, injection, virtual adapter or kernel hook.
- No PS5 native traffic interception (Track B stays research-only / BLOCKED).
- No change to SharePlay, voice, messaging, profiles, pairing, notifications or game detection.
- No production, VPS, DNS, firmware or account changes.
