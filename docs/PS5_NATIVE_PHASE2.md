# AKENO NETWORK Native — Phase 2 plan: native social screens over the daemon

Status: **approved direction** (owner, 2026-10-17, after the 12.20 result
[E14](PS5_NATIVE_ADR.md#2-evidence)). Increment 2.1 is being implemented in this branch; nothing in
Phase 2 is verified on a PS5 until the owner reports it. Nothing is merged or deployed
automatically.

## Goal and rules

Native Friends, Profiles, Activity, Messages, Parties and Settings on the PS5, using the existing
AKENO backend, with these rules from the owner:

| Rule | How Phase 2 keeps it |
| --- | --- |
| The daemon stays responsible for realtime, notifications and game detection | The native app never opens a WebSocket, never sends presence or game heartbeats and never shows system notifications. It reads the daemon's existing event ring and state |
| No competing realtime sessions | One server session per console: the daemon's. The native app talks only to the daemon (loopback); CI fails if native code references `akeno_realtime_run` |
| Preserve existing functionality, Account Bridge and production | No server, API, database, web or migration change. The daemon release ELF stays the hardware-tested Alpha 0.5 RC file (ps5.yml proves it on every commit); the IPC is a **separate daemon build** |
| Secure local IPC | Per-launch secret in a private file, loopback only, allow-list, existing Host/Origin defences; details below |
| Incremental, CI-tested, hardware-testable | One increment per hardware test; each ships native + daemon builds, the previous tested builds as byte-verified fallbacks, and a test plan |

## Architecture

```
AKENO Native (foreground, homebrew launcher)
   │  HTTP/1.1 on 127.0.0.1:8642, header X-Akeno-Native-Key (per daemon launch)
   ▼
AKENO daemon, native-IPC build (background, ELF loader)
   ├─ existing local API: /api/state, /api/events, /api/inbox, /api/v1/* proxy, /api/media, ...
   ├─ realtime WebSocket, notifications, game detection   (unchanged, sole owner)
   ▼  one HTTPS + WSS session with the device token
AKENO server (unchanged)
```

The daemon's browser page already uses exactly this local API with a per-launch page secret. The
native-IPC build accepts a **second** per-launch secret for the native app, on a smaller
allow-list. Data and events reach the native app the way they reach the page: REST through the
daemon's proxy (`/api/v1/friends`, `/api/v1/users/:handle`, ...) and the daemon's event ring
(`/api/events?after=N`), which carries every realtime event the daemon receives.

### The native key

| Property | Value |
| --- | --- |
| Created by | the daemon (native-IPC build), at every start, 32 random bytes as 64 hex digits from the same generator as the page secret |
| Stored | `/data/akeno/native.key`, mode 0600, written atomically like `instance.key`; deleted when the daemon stops |
| Read by | AKENO Native only: `lstat` must show a regular file, not a link, owner-only mode, exactly 64 hex digits |
| Sent as | `X-Akeno-Native-Key` on loopback requests; never logged (the native log redacts `X-Akeno-*` values) |
| Compared | constant time (`ui_http_secret_equal`), like every daemon secret |

**What it protects against.** Web pages (the PS5 browser included) cannot read the file, and a
custom header from a page needs a CORS preflight the daemon never grants; the daemon's existing
exact-Host (DNS rebinding) and Origin checks still run first, and requests carrying the native
key must have no `Origin` at all. **What it does not protect against:** another program on the
console that can read `/data/akeno` can already read `device.json`, the device token itself — the
key gives such a program nothing it did not have. This is documented, not hidden.

### What the native key may call (IPC protocol 1)

| Route | Use |
| --- | --- |
| `GET /api/native/hello` | IPC version, daemon version and build, pairing state; logged once per connection burst |
| `GET /api/state` | pairing, account, realtime state, the console's detected game, inbox summary, notification settings |
| `GET /api/events?after=N` | the event ring: presence, friends, messages, parties, invitations, activity, realtime state |
| `GET /api/inbox`, `POST /api/inbox/seen` | recent messages (2.2) |
| `/api/v1/*` | the daemon's existing proxy and **its existing allow-list** (users, presence, friends, conversations, groups, parties, game sessions, invitations, notifications, `GET /devices/current`); the server still enforces the device's scopes |
| `GET /api/media`, `GET /api/local-art` | pictures (later increment) and installed-game icons |
| `POST /api/settings` | **only** `notifyMessages`, `notifySocial`, `activityEnabled` (2.4); anything else in the body is refused |
| `POST /api/realtime/reconnect` | ask the daemon to reconnect its own session |

Never with the native key: pairing, unpairing, server or console-name changes, the controller
shortcut, diagnostics and audio-lab actions, test notifications, opening the browser
(`/api/instance/show`), stopping the daemon. Those stay with the browser page.

### The daemon build

`make -C clients/ps5/console NATIVE_IPC=1` → `build-ps5-native-ipc/akeno-network-ps5-native-ipc.elf`,
build ID `<commit>-ps5-native-ipc`, version still `0.5.0-alpha` (what the server sees is
unchanged). All IPC code is under `#ifdef AKENO_NATIVE_IPC`. CI proves on every commit that:

- the release ELF built from this commit with the RC's build ID is **byte-identical** to the
  hardware-tested Alpha 0.5 RC (`ps5.yml`, unchanged);
- the native-IPC ELF passes the daemon's import gate and imports **exactly** what the release ELF
  imports (no new system function);
- the host build of the native-IPC daemon passes end-to-end tests against the real AKENO server
  (below).

The `/api/instance` answer of this build adds `"nativeIpc":1`; the native app uses that to tell an
IPC daemon from the release daemon. With the release daemon the native app keeps working as in
0.1 (connection check, controller, audio) and shows how to get the social screens.

## Increments

| # | Content | Writes to the server | Daemon build |
| --- | --- | --- | --- |
| **2.1** (this build) | IPC protocol 1; native IPC client; **Friends** (presence, current game, incoming request count, live presence updates from the event ring), **Profiles** (yours and your friends': name, presence, status, bio, game, favourite games, badges count, mutual friends, recent games), **Activity** (this console's detected game from the daemon, your recent games, friends playing now); Connection screen shows the IPC state | **none** — the native 2.1 IPC client has no write function at all | native-IPC 1 |
| 2.2 | **Messages**: conversations, history, send text (system keyboard), mark read; in-app message banner; the daemon skips its own toast for a conversation that is open in the native app (heartbeat) | messages | native-IPC 2 (heartbeat) |
| 2.3 | **Parties**: current party, members, party chat, join/leave, invitations inbox. No voice (voice roadmap unchanged) | party actions | native-IPC 2 |
| 2.4 | **Settings**: notification and activity toggles through the daemon, presence preference; **pictures**: avatars and banners through `/api/media` (JPEG fallback variant, size-capped decoder with fuzz tests in CI) | settings, presence | native-IPC 2 |
| 2.5 | Parity review against the browser page; the owner decides about the legacy browser page (it stays until then) | — | — |

Each increment: unit tests (parsers, model), the UI integration test with a fake daemon, the
**end-to-end test** (real server + native-IPC daemon host build + native host build), PS5 builds
(native + native-IPC daemon) with import gates, reproducible builds, a hardware test plan, and the
previously tested builds as byte-verified fallbacks.

## Testing

| Level | What runs | Where |
| --- | --- | --- |
| Unit | JSON parsing of every server and daemon answer used (friends, requests, profile, activity, state, events), presence updates, sorting, text | `clients/ps5-native/tests/unit` (ASan/UBSan, Valgrind) |
| UI integration | native app + fake daemon (IPC protocol, events, a daemon without IPC, a wrong key, a 401) through the virtual controller; screenshots; checks that **only GET** reached the daemon in 2.1 | `tests/integration/run.mjs` |
| End to end | disposable PostgreSQL + the real server build + TLS with a throwaway CA + the **native-IPC daemon (host build)** paired through its own page API + the native app: friends and profiles from the real API, a presence change and a friend request arriving live through the event ring, IPC security cases (no key, wrong key, forbidden routes, Origin, key file mode and removal, the release daemon ignoring the header) | `tests/e2e/run.mjs`, "PS5 native client" workflow |
| PS5 build | native ELF + native-IPC daemon ELF, import gates, reproducibility, fallbacks | same workflow |
| Hardware | the owner, by hand, with [HARDWARE_TEST_PLAN_PS5_NATIVE_0.2.md](HARDWARE_TEST_PLAN_PS5_NATIVE_0.2.md) | PS5 12.20 |

## Rollback

Native: replace `/data/homebrew/AKENONative` with the previous folder from `fallback/` (or delete
it). Daemon: stop the native-IPC daemon with **Exit AKENO** on its browser page and send the
hardware-tested release ELF again; `/data/akeno/native.key` is deleted when the daemon stops and is
harmless if left behind (the next daemon start replaces it, the release daemon ignores it). No
pairing, settings or server data change when switching between the daemon builds.
