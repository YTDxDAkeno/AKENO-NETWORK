# AKENO LAN Play — security

LAN Play is off by default and, in this change, carries no traffic. This document states the
security model for the control plane that exists now and the rules a future transport must follow.

## Control plane (implemented now)

- **No unauthenticated access.** Every LAN Play route requires an AKENO identity. Creating, joining,
  ending and connecting require a signed-in **browser session**; paired devices may only **read**
  status (scope `parties:read`), never create or connect. This mirrors SharePlay.
- **Authorization reuses the party system.** Only current members of a party can see or use its LAN
  Play. There is no second account system, permission store or membership list.
- **Expiring connection credentials.** A credential is a 256-bit random opaque token
  (`lanc_…base64url`). Only its SHA-256 hash is kept in memory, bound to `{sessionId, userId}` with a
  short TTL (`LAN_PLAY_CREDENTIAL_TTL_SECONDS`, default 120 s). A participant refreshes by calling
  connect again; the raw token is returned once and never logged.
- **Revocation is immediate and total.** A participant's credential is invalidated (and its epoch
  bumped so an old token can never be reused) when it leaves or is removed from the party, when a
  block exists either way with the host, when it is suspended or deleted, or when its browser session
  is signed out or revoked. The whole session — and all its credentials — ends when the host leaves,
  signs out, the party closes, the host or leader ends it, or the session passes its TTL. These use
  the same hooks SharePlay uses, so the two cannot drift apart.
- **In-memory only, safe failure.** No LAN Play state is persisted. A restart ends every session and
  credential; clients simply see the session disappear.
- **No traffic, no claims.** With no transport, connect returns `503 LAN_TRANSPORT_UNAVAILABLE` and
  the UI shows "Transport not available". Nothing reports a connection that does not exist.
- **No payloads or secrets logged.** The control plane logs party/session IDs and reasons only, never
  credentials.

## Data plane (rules a future transport MUST follow)

These are requirements on any adapter wired behind `LanTransportAdapter`, carried from the spec:

1. No unauthenticated tunnels; every connection presents a current AKENO credential.
2. No public/arbitrary UDP forwarding; no open relay. A session must not become a VPN or proxy.
3. No default access to a participant's whole home network. Traffic is scoped to the selected game
   **device** (by MAC) and to the discovery/ports a verified game needs.
4. Explicit user consent for any elevated OS/network permission; no silent packet-driver install.
5. Well-reviewed crypto only (e.g. the WireGuard/QUIC/TLS ecosystems); no home-grown ciphers.
6. Never log packet payloads or credentials; diagnostics redact private data on export.
7. Keep game traffic isolated from AKENO's control traffic.
8. Stop forwarding instantly when authorization is revoked (the control plane already enforces the
   credential lifecycle above).
9. Rate-limit and bound queues; prevent amplification.
10. Clean uninstall: restore any local network change (adapters, routes, firewall rules).
11. **Never** modify the PS5's DNS (`62.210.38.117`), PSN routing, Sony certificates or any console
    system setting. The bridge is host-side and device-scoped.
12. Relay traffic must not be deployed to the production VPS without explicit approval, and must be
    bandwidth/CPU accounted.

## Threats considered

- **Stolen credential** → short TTL, single-session binding, epoch invalidation on any change.
- **Removed/blocked/suspended member still connected** → control-plane revocation fires on each such
  event; the transport must honour it immediately.
- **Replay / stale session** → epochs and in-memory-only state; a restart invalidates everything.
- **Privilege/abuse via the console** → devices can only read LAN Play; they cannot create or connect.
- **Scope creep into the home network** → device-MAC scoping and per-game port/discovery filters are
  mandatory for the transport; the control plane never widens this.

See `LAN_PLAY_ARCHITECTURE.md` for the adapter seam and `docs/SECURITY.md` for AKENO-wide security.
