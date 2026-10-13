# AKENO NETWORK

A private, self-hosted gaming community platform for a group of friends: accounts, profiles,
friends, real-time presence, direct and group chat, text parties and a game-session lobby
directory — running on your own PC or a small Linux VPS.

> **Independence notice.** AKENO NETWORK is an independent project. It is **not** affiliated
> with, endorsed by, or connected to Sony Interactive Entertainment, PlayStation Network or any
> console manufacturer. It does not provide PSN sign-in, native console friends, console party
> voice, Share Play, console game presence or game multiplayer networking, and it does not
> integrate with the PS5's own Game Base. The server requires **no** console, no PSN account and
> no console modification. See [docs/PS5_FEASIBILITY.md](docs/PS5_FEASIBILITY.md) and
> [docs/PS5_GAMEBASE_RESEARCH.md](docs/PS5_GAMEBASE_RESEARCH.md).

## Features (V1)

| Area | What works |
| --- | --- |
| Accounts | Invitation-based registration (open mode for development), Argon2id passwords, opaque server-side sessions in HttpOnly cookies, CSRF protection, multi-device sign-in, session list and revocation, password change, account deletion |
| Profiles | Display name, 16 original preset avatars, bio, status message, favorite games, self-reported activity, privacy settings |
| Friends | Search, requests (send/accept/decline/cancel), removal, blocking, race-safe relationship rules |
| Presence | Online / Away / Do Not Disturb / Invisible / Offline from live WebSocket connections, multi-device aware, privacy-filtered, last seen |
| Messaging | Persistent direct messages delivered in real time, idempotent retries, cursor pagination, unread counts, typing indicators, reconnect recovery |
| Groups | Owner / moderator / member roles, invitations, open groups, group chat, ownership transfer, deletion |
| Parties | Private text parties with leader, invitations, capacity, one party per user (voice is **not** included in this release) |
| Game sessions | Lobby directory with search and filters, visibility, instant or approval-based joining, invitations, capacity enforced under concurrency, connection details for participants only |
| Paired devices | Code-based device pairing approved on the website (no password on the device), scoped and revocable device tokens for API and realtime, paired-device management in Settings |
| Device client | Portable C11 client library + `akeno-cli` (`clients/ps5`): pairing, friends, presence, messages, invitations, parties, game sessions, realtime with bounded reconnects. Tested on Linux with OpenSSL and mbedTLS |
| PS5 console app (Alpha 0.1) | Payload for a jailbroken PS5: verified HTTPS/WSS, code pairing, a controller-friendly page in the system browser, system notifications ([docs/PS5_CLIENT.md](docs/PS5_CLIENT.md)). ELF built in CI and tested on Linux; **not yet run on a PS5** |
| Administration | Invitation codes (shown once, stored hashed), member suspension and roles, audit log, system stats |
| Operations | Docker Compose deployment, Caddy automatic HTTPS, health checks, backups and restore, structured logs |

## Quick start

**Windows 11:** follow [docs/WINDOWS_SETUP.md](docs/WINDOWS_SETUP.md).
**Linux VPS (production):** follow [docs/VPS_DEPLOYMENT.md](docs/VPS_DEPLOYMENT.md).

### Try it locally with Docker (any OS with Docker)

```bash
cp .env.example .env
# Edit .env: set POSTGRES_PASSWORD to a long random value and
# AKENO_PUBLIC_ORIGIN=http://localhost:8080
docker compose up -d --build
docker compose exec app node dist/cli.js create-admin --online-id admin
docker compose exec app node dist/cli.js create-invitation --max-uses 5
```

Open <http://localhost:8080>, sign in as `admin`, and share invitation codes with friends.

### Develop locally (Node.js 22 + pnpm 10)

```bash
cp .env.example .env               # adjust DATABASE_URL if needed
docker compose -f compose.dev.yaml up -d   # or use an existing PostgreSQL 16
pnpm install
pnpm db:migrate
pnpm admin:create --online-id admin
pnpm invite:create --max-uses 5
pnpm dev                           # web on http://localhost:5173, API on :3000
```

Full instructions: [docs/LOCAL_SETUP.md](docs/LOCAL_SETUP.md).

## Commands

| Command | Purpose |
| --- | --- |
| `pnpm dev` | Run the API (tsx watch) and the web client (Vite) together |
| `pnpm build` | Build the shared package, server bundle and web client |
| `pnpm lint` / `pnpm typecheck` | ESLint and TypeScript strict checks |
| `pnpm test` | Unit and integration tests (server tests need PostgreSQL) |
| `pnpm test:e2e` | Build, then run the Playwright browser acceptance test |
| `pnpm db:migrate` / `pnpm db:generate` | Apply / generate SQL migrations |
| `pnpm db:seed` | Development demo data (refused in production) |
| `pnpm admin:create --online-id <id>` | Create an administrator |
| `pnpm invite:create [--max-uses n]` | Create a registration invitation code |
| `pnpm perf` | Load test against a disposable instance |
| `pnpm test:client` | Build and test the C device client (unit + end-to-end, sanitizers) |

## Repository layout

```
apps/server        Fastify API, WebSocket gateway, PostgreSQL schema & migrations, CLI
apps/web           React web client (Vite, Tailwind CSS), unit and E2E tests
packages/shared    Zod schemas, API contracts and realtime event types used by both
clients/ps5        Portable C11 device client (library, akeno-cli), PS5 console app (app/, console/), tests
infrastructure     Dockerfile, Caddyfile, backup/restore, load-test and E2E scripts
docs               Architecture, API, security, deployment and status documentation
```

## Documentation

- [Architecture](docs/ARCHITECTURE.md) · [API](docs/API.md) · [Database](docs/DATABASE.md)
- [Local setup](docs/LOCAL_SETUP.md) · [Windows 11](docs/WINDOWS_SETUP.md) · [VPS deployment](docs/VPS_DEPLOYMENT.md)
- [Security](docs/SECURITY.md) · [Testing](docs/TESTING.md) · [Performance](docs/PERFORMANCE.md) · [Troubleshooting](docs/TROUBLESHOOTING.md)
- [Game integration (future)](docs/GAME_INTEGRATION.md) · [PS5 feasibility](docs/PS5_FEASIBILITY.md) · [Licenses](docs/LICENSES.md)
- Devices and PS5: [PS5 console app](docs/PS5_CLIENT.md) · [Device pairing](docs/DEVICE_PAIRING.md) · [Client installation](docs/INSTALLATION.md) ·
  [Firmware compatibility](docs/FIRMWARE_COMPATIBILITY.md) · [Safety](docs/SAFETY.md) ·
  [Game Base research](docs/PS5_GAMEBASE_RESEARCH.md) · [PSN compatibility research](docs/PSN_COMPATIBILITY_RESEARCH.md)
- [Project status](docs/PROJECT_STATUS.md) — what is implemented, tested, incomplete or deferred

## License

MIT — see [LICENSE](LICENSE). Third-party licenses: [docs/LICENSES.md](docs/LICENSES.md).
