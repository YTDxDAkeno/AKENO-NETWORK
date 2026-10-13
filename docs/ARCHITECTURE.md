# Architecture

AKENO NETWORK is a **modular monolith**: one Node.js process serves the REST API, the
WebSocket gateway and the built web client from a single origin, backed by one PostgreSQL
database. This keeps a 5–20 user deployment simple to run on a small VPS while keeping clear
module boundaries for later growth.

```
 Browser (desktop / mobile)          AKENO device client (clients/ps5, C11)
        │  HTTPS + WSS (cookie,              │  HTTPS + WSS (Authorization: Bearer
        │  same origin)                      │  device token, scoped, revocable)
        ▼                                    ▼
 ┌──────────────┐   reverse proxy, automatic TLS (Let's Encrypt)
 │    Caddy     │
 └──────┬───────┘
        │  HTTP on the private Docker network
        ▼
 ┌──────────────────────────────────────────────┐
 │  AKENO app (Node.js 22, Fastify 5)           │
 │  ├─ REST API      /api/v1/*                  │
 │  ├─ WebSocket     /ws  (auth + origin check) │
 │  ├─ Static web    / (React SPA build)        │
 │  ├─ RealtimeHub + PresenceService (memory)   │
 │  └─ Services: auth, users, friends, messages,│
 │     groups, parties, game sessions, ...      │
 └──────┬───────────────────────────────────────┘
        │  pg connection pool (default 10)
        ▼
 ┌──────────────┐
 │ PostgreSQL 16│  authoritative durable state, internal network only
 └──────────────┘
```

## Packages

| Package | Responsibility |
| --- | --- |
| `packages/shared` | Zod request schemas, response contracts (DTO types), realtime event types, product constants (limits, avatars, platforms). Imported as TypeScript source by both apps, so validation rules are identical on client and server. |
| `apps/server` | Fastify application, PostgreSQL schema and migrations (Drizzle ORM), services, WebSocket gateway, operator CLI. Bundled with esbuild into `dist/main.js` and `dist/cli.js`. |
| `apps/web` | React 19 single-page application (React Router 7, TanStack Query 5, Tailwind CSS 4), built with Vite. |
| `clients/ps5` | Portable C11 device client (library + `akeno-cli`): pairing, REST, realtime, capability policy. Built and tested on Linux; the reference for a future console build. Not a pnpm workspace member. |

## Server modules

`apps/server/src` is organized by responsibility:

| Path | Contents |
| --- | --- |
| `app/` | `build-app.ts` (plugin and route wiring), `context.ts` (service container), `logger.ts` |
| `config/` | Environment validation (`env.ts`), `.env` loader for development |
| `database/` | Drizzle schema per domain, generated SQL migrations, migration runner with advisory lock |
| `middleware/` | Session resolution, CSRF and origin checks, error mapping |
| `modules/<domain>/` | `service.ts` (business rules, transactions) + `routes.ts` (HTTP handlers): auth, users/profiles, friends, messages, groups, parties, game-sessions, invitations, notifications, administration, accounts, health |
| `realtime/` | `RealtimeHub` (connection registry), `PresenceService`, pure `presence-rules.ts`, `gateway.ts` |
| `services/` | Argon2id password hashing, audit log, static web hosting |
| `utils/` | Error types, crypto helpers, validation, after-commit effects |

Each service receives an `AppContext` and resolves collaborators lazily, so cross-module
calls (for example accepting an invitation, which joins a group, party or session) are
explicit and happen inside one database transaction.

### Request flow

1. `onRequest` hooks resolve the session cookie (API and `/ws` only) and, for state-changing
   API requests, verify the `Origin`/`Referer` against the configured origins.
2. The rate limiter (keyed by user when authenticated, by IP otherwise) runs per route.
3. Route `preHandler` guards require a session and a valid `X-CSRF-Token` for unsafe methods;
   administration routes additionally require the `admin` role.
4. Handlers validate input with the shared Zod schemas, call a service, and return a DTO.
5. Errors are mapped to `{ error: { code, message, fields?, requestId } }` without internals.

### Transactions and side effects

Multi-step state changes run in a single PostgreSQL transaction. Realtime events and
notification pushes are collected in an `Effects` queue and only run **after commit**, so
clients are never told about changes that were rolled back. Concurrency-sensitive rules are
enforced by the database (unique and partial unique indexes, CHECK constraints, row locks);
see [DATABASE.md](DATABASE.md).

## Paired devices

Non-browser clients authenticate with device tokens issued through code-based pairing
([DEVICE_PAIRING.md](DEVICE_PAIRING.md)). `middleware/auth.ts` resolves `Authorization: Bearer`
before cookies (and then ignores cookies), marks the identity as `credential: 'device'` with its
scopes, and a global `preValidation` hook enforces the default-deny route table in
`modules/devices/scopes.ts` before any route guard runs. Device connections are ordinary
`ClientConnection`s in the realtime hub (keyed by device ID), so presence, fan-out and
revocation work exactly as for browser tabs.

```
clients/ps5
  include/akeno/   akeno.h (public API), caps.h (capability policy), json.h
  src/             buf, json, url, http, ws, backoff, caps        (platform-neutral core)
                   client.c (config, credentials, requests, pairing), realtime.c
                   transport.h + transport_openssl.c              (the only socket/TLS code)
  cli/main.c       akeno-cli
  tests/           unit tests (C) and the end-to-end runner (Node)
```

Porting to another platform means implementing `transport.h` (sockets + verifying TLS) and a
front end; the core has no platform dependencies.

## Realtime design

- One authenticated WebSocket per browser tab or paired device at `/ws`. Browsers present a
  valid session cookie and an allowed `Origin`; devices present a bearer token with the
  `realtime` scope. Identity always comes from server-side state.
- Events are versioned envelopes `{ v: 1, type, data, ts }` defined in
  `packages/shared/src/contracts/realtime.ts`. Clients may only send `ping`, `presence.idle`
  and `typing`; anything else closes the socket with code 4400.
- Heartbeats: the client sends `ping` every 25 s and reconnects if no `pong` arrives within
  10 s; the server sends protocol pings every 20 s and terminates connections silent for 70 s.
- Session revocation (logout, password change, suspension, account deletion) closes the
  affected sockets with code 4401, after which the client stops reconnecting.
- **PostgreSQL is the source of truth.** WebSockets only reduce latency. After every `hello`
  (including the first one after a page load) the web client fetches missed messages with the
  `after` cursor for every cached conversation; after a reconnect it also refetches all other
  server state. Message events that arrive while a conversation is still loading are applied
  once that fetch settles (`apps/web/src/lib/message-sync.ts`).

### Presence

Presence is computed, never stored as "online":

- A user is connected while at least one authenticated WebSocket is alive (multi-device safe).
- Effective status = `offline` (no connections) → `invisible` / `dnd` / `away` preference →
  `away` if every connection reports idle (5 minutes without input) → `online`.
- Viewers receive a privacy-filtered `PresenceView`: blocked relationships and audiences not
  allowed by `presenceVisibility` see `offline` with no activity or last-seen.
- `last_seen_at` only advances while the user is *visibly* online, so invisible users do not
  leak activity through last-seen timestamps.
- State is in memory for this single instance. After a restart everyone is offline until
  their clients reconnect (automatic within seconds).

## Web client

- Server state lives in TanStack Query caches. `RealtimeProvider` maps each server event to a
  cache update or invalidation (`apps/web/src/app/providers/RealtimeProvider.tsx`).
- Sending a message is optimistic: a pending bubble with a client-generated UUID is shown, the
  server deduplicates retries by that UUID, and the confirmed message replaces it.
- The CSRF token is held in memory only; the session cookie is HttpOnly. Nothing
  security-relevant is written to `localStorage` (only theme and controller preferences).

## Scaling path (not needed for V1)

The V1 target is one instance for 5–20 concurrent users; measured headroom is in
[PERFORMANCE.md](PERFORMANCE.md). To run several instances later:

1. Replace `RealtimeHub` fan-out with a shared bus behind the same interface (PostgreSQL
   `LISTEN/NOTIFY` or Redis pub/sub) and move presence counters to a shared store.
2. Move the in-memory login throttle to the shared store.
3. Keep sticky sessions unnecessary: authentication is already stateless per request
   (cookie → database lookup).

## Technology choices

| Choice | Reason |
| --- | --- |
| Fastify 5 + `@fastify/websocket` | Fast, typed hooks for auth/CSRF, built-in pino logging, upgrade rejection before sockets exist |
| PostgreSQL 16 + Drizzle ORM 0.45 | Transactions and constraints for race-free rules; typed queries; plain SQL migrations |
| Zod 4 | One validation source for client and server |
| Argon2id (`@node-rs/argon2`) | OWASP-recommended password hashing; prebuilt binaries for glibc, musl (Alpine) and Windows |
| React 19, React Router 7, TanStack Query 5, Tailwind CSS 4, Vite | Mature, well-documented stack; Router 7 chosen over 8 for proven stability |
| TypeScript 5.9 | `typescript-eslint` 8 supports TypeScript < 6.1 |
| No Redis / message broker | Not needed for one instance; adapter boundary documented above |

## Reference projects (researched, not used as code)

The following were studied for concepts only. No code was copied. Evidence levels follow the
project brief (1 = implemented and tested, 5 = not available).

| Project | Finding | Level | Use in AKENO NETWORK |
| --- | --- | --- | --- |
| OrbisNet | Developer-described P2P online layer for jailbroken PS4; closed source, no private-server API published | 3 | Not used |
| Project BlueSphere | Public docs describe login/verify endpoints and UDP presence relay; no verified self-hostable backend | 3 | Separation of identity, auth and presence informed the design; no dependency |
| Omega Dashboard (PS5) | Source-visible homebrew client and Node/PostgreSQL server (GPL-3.0-or-later) | 2 | Architecture reference only; independent implementation; not tested on hardware |
| CutTheCord Standalone | MIT, Fastify/React/PostgreSQL/LiveKit reference; README warns of an unauthenticated admin API | 2 | Not used; AKENO admin routes require an admin session plus CSRF |
| Matrix Synapse | Mature AGPL/commercial chat server | 2 | Rejected for V1 complexity |
| LiveKit / coturn | Apache-2.0 SFU / BSD-3 TURN server | 2 | Candidates for the deferred voice phase |
