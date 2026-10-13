# Performance and capacity

This document separates **measured results**, **calculated estimates** and **unverified
expectations**. No benchmark here was run on a real 1 vCPU / 2 GB VPS yet.

## How to reproduce

1. Start a disposable instance with rate limiting disabled (all simulated users share one IP
   and would otherwise be rate limited, which is correct production behavior):
   ```bash
   pnpm build
   NODE_ENV=production DATABASE_URL=postgres://…/akeno_load PUBLIC_ORIGIN=http://localhost:3300 \
   PORT=3300 MIGRATE_ON_START=true RATE_LIMIT_ENABLED=false node apps/server/dist/main.js
   ```
2. Create an admin for the run (`node apps/server/dist/cli.js create-admin --online-id loadadmin`).
3. Run the load test:
   ```bash
   AKENO_URL=http://localhost:3300 LOAD_ADMIN_ONLINE_ID=loadadmin LOAD_ADMIN_PASSWORD=… \
   node infrastructure/scripts/load-test.mjs --users 20 --messages 200 --concurrency 5 \
     --server-pid <server PID> --json results.json
   ```

The script (`infrastructure/scripts/load-test.mjs`) registers N users through the API (with
a multi-use invitation), signs them in, connects one WebSocket per user, creates a ring of
friendships, and measures profile and friend-list retrieval, direct-message send
acknowledgement and **end-to-end delivery latency (REST send → recipient's WebSocket
event)**, party join updates and concurrent game-session joins. It samples server memory,
connections and the database pool from `/api/v1/admin/stats` and, optionally, the process
with `ps`. Raw results are in `docs/perf/*.json`.

## Test environment (measured runs)

| Item | Value |
| --- | --- |
| Date | 2026-10-13 |
| Host | Cloud development container, 4 vCPU Intel Xeon @ 2.10 GHz, 15 GiB RAM |
| Software | Node.js 22.22.0, PostgreSQL 16.15 (default Ubuntu configuration), production server bundle |
| Argon2id | Production parameters (m = 19 MiB, t = 2, p = 1) |
| Load generator | Same machine (adds client-side overhead; network latency ≈ 0) |
| Client concurrency | 5 requests in flight (10 for the 100-user run) — far busier than real users |

## Measured results

All values are milliseconds observed by the load-test client.

### 20 users, 200 messages, 4 vCPU (`docs/perf/load-20-users.json`)

| Operation | p50 | p95 | max |
| --- | --- | --- | --- |
| Registration (includes Argon2id hashing) | 64 | 134 | 139 |
| Login (Argon2id verification) | 40 | 49 | 64 |
| WebSocket connect + authenticate | 10 | 16 | 17 |
| Profile retrieval | 16 | 37 | 52 |
| Friend list retrieval | 9 | 19 | 19 |
| Message send → acknowledged (stored) | 21 | 34 | 50 |
| **Message send → delivered to recipient's WebSocket** | **21** | **35** | **51** |
| Party join → leader receives update | 15 | 19 | 19 |
| Game session join (19 joiners, 5 concurrent) | 47 | 74 | 74 |

Server after the run: RSS 131 MB, V8 heap 38 MB, 20 WebSocket connections, database pool
10 connections (all idle after the run). No warnings or errors were logged.

### 20 users, 200 messages, app **and** PostgreSQL pinned to one CPU core (`docs/perf/load-20-users-1cpu.json`)

This approximates the CPU budget of a 1 vCPU VPS (`taskset -c 0` for the Node.js process and
all PostgreSQL processes; the load generator ran on other cores). Memory was not limited.

| Operation | p50 | p95 | max |
| --- | --- | --- | --- |
| Registration | 197 | 382 | 402 |
| Login | 133 | 160 | 185 |
| WebSocket connect | 13 | 33 | 43 |
| Profile retrieval | 38 | 46 | 47 |
| Friend list retrieval | 18 | 32 | 34 |
| Message send → acknowledged | 44 | 67 | 103 |
| **Message send → delivered** | **44** | **67** | **103** |
| Party join → update | 12 | 21 | 21 |
| Game session join | 78 | 129 | 129 |

Server RSS 130 MB, heap 41 MB. Registration and login are slower here because Argon2id is
CPU-bound and five run at once on one core — this is intended (it slows down password
guessing) and only affects sign-in, not chat.

### 100 users, 1000 messages, 4 vCPU (`docs/perf/load-100-users.json`)

| Operation | p50 | p95 | max |
| --- | --- | --- | --- |
| Registration | 73 | 122 | 157 |
| Login | 56 | 72 | 74 |
| WebSocket connect | 10 | 24 | 27 |
| Profile retrieval | 16 | 23 | 24 |
| Friend list retrieval | 8 | 12 | 17 |
| **Message send → delivered** | **27** | **39** | **50** |
| Game session join (63 joiners, 10 concurrent) | 66 | 121 | 131 |

Server RSS 134 MB, heap 35 MB with 100 WebSocket connections.

### Other observations

- The sum of PostgreSQL process RSS on the test host was 418 MB. This **over-counts** shared
  buffers (each backend's RSS includes shared pages); it is an upper bound, not the real
  footprint. The Compose file limits the database container to 512 MB with
  `shared_buffers=128MB` and `max_connections=40`.
- The production Docker image is 303 MB (Alpine, Node.js 22, production dependencies only).
- The database for the 100-user run (users, 1000 messages, sessions) was 9 MB.

## Calculated estimates

- **Memory on a 2 GB VPS:** app container ≈ 130–140 MB measured (limit 512 MB), PostgreSQL
  container limited to 512 MB, Caddy typically < 50 MB (limit 128 MB), OS ≈ 200–300 MB. This
  leaves roughly 1 GB of headroom. Estimate, not measured on such a VPS.
- **WebSocket memory:** RSS changed by only ~3 MB between 20 and 100 connections, i.e. tens of
  kilobytes per idle connection.
- **Message throughput:** the 1-core run sustained roughly 100 messages/second in bursts
  (200 messages in ~2 s with 5 in flight). A group of 20 friends chatting actively produces a
  few messages per second.

## Unverified expectations

- 5–20 concurrent active users on a 1 vCPU / 2 GB VPS should see sub-100 ms message delivery
  plus network latency. **Not yet verified on real VPS hardware**; run the load test on your
  server to confirm.
- Disk I/O on budget SSD VPS plans was not measured.
- Behavior over real mobile networks (high latency, frequent reconnects) was only tested
  through the reconnect logic in automated tests, not on real networks.

## Voice and TURN

Voice chat is **not implemented** in V1, so no voice signaling or TURN numbers exist. When
voice is added (see PROJECT_STATUS.md), measure separately:

- Signaling load on the app (small: a few WebSocket messages per participant join).
- Media: peer-to-peer full mesh sends each speaker's audio to every other participant
  (`n·(n−1)` streams), which limits practical mesh parties to about 4–6 people.
- TURN relays all media for users behind restrictive NAT. Its cost is **network bandwidth**
  (roughly 30–60 kbit/s per Opus audio stream in each direction, per relayed participant),
  separate from the app's CPU and RAM, and should be budgeted against the VPS traffic quota.
