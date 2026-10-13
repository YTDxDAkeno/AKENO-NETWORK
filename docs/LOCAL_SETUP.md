# Local development setup (Linux and macOS)

Windows 11 users: see [WINDOWS_SETUP.md](WINDOWS_SETUP.md). The steps are the same, but the
commands are shown for PowerShell there.

## Requirements

| Tool | Version | Notes |
| --- | --- | --- |
| Node.js | 22 LTS (22.12 or newer) | Node 24 LTS is expected to work but V1 was tested on 22.22 |
| pnpm | 10.x | `npm install --global pnpm@10` (or `corepack enable`) |
| PostgreSQL | 16 | Via Docker (`compose.dev.yaml`) or a local installation |
| Docker | Optional | Only needed for the bundled PostgreSQL or the production stack |

## 1. Install dependencies

```bash
git clone <your repository URL> akeno-network
cd akeno-network
pnpm install
```

## 2. Start PostgreSQL

**Option A — Docker (recommended):**

```bash
docker compose -f compose.dev.yaml up -d
```

This starts PostgreSQL 16 on `127.0.0.1:5432` with database `akeno_dev`, user `akeno` and the
password `change-me-local-dev-password` (override with `DEV_POSTGRES_PASSWORD`).

**Option B — existing PostgreSQL:** create a role and database. The role needs `CREATEDB` if
you want to run the server integration tests, which create disposable databases.

```sql
CREATE ROLE akeno WITH LOGIN PASSWORD 'choose-a-password' CREATEDB;
CREATE DATABASE akeno_dev OWNER akeno;
```

## 3. Configure the environment

```bash
cp .env.example .env
```

For development the defaults are fine. Make sure `DATABASE_URL` matches step 2 and that
`PUBLIC_ORIGIN=http://localhost:5173` (the Vite dev server). The `.env` file is ignored by Git.

## 4. Create the schema and the first administrator

```bash
pnpm db:migrate
pnpm admin:create --online-id admin        # prompts for a password (12+ characters)
pnpm invite:create --max-uses 5            # prints an invitation code once
```

Optional demo data (never in production): `pnpm db:seed` creates `alice`, `bob` and `carol`
with the password `akeno-dev-password`.

## 5. Run the app

```bash
pnpm dev
```

- Web client: <http://localhost:5173>
- API: <http://localhost:3000> (the Vite dev server proxies `/api`, `/ws` and `/health`, so
  the browser only talks to one origin, just like production)

Sign in as your administrator, open **Administration → Invitations** to create codes, and
register test users in a second browser profile or a private window.

### Testing from phones and tablets on your LAN

1. Start the API with `pnpm --filter @akeno/server dev` and, in a second terminal, Vite on all
   interfaces: `pnpm --filter @akeno/web dev --host`.
2. Add the LAN origin to `.env`: `ADDITIONAL_ORIGINS=http://192.168.1.20:5173` (your IP).
3. Restart `pnpm dev` and open `http://192.168.1.20:5173` on the device.

Plain HTTP on a LAN works for testing (cookies are not marked `Secure`), but use HTTPS for any
deployment reachable from the internet.

## 6. Quality checks

```bash
pnpm lint
pnpm typecheck
pnpm test          # needs PostgreSQL; see TESTING.md for TEST_DATABASE_ADMIN_URL
pnpm test:e2e      # builds everything and runs the Playwright acceptance test
```

## Production build without Docker

```bash
pnpm build
NODE_ENV=production \
DATABASE_URL=postgres://akeno:...@127.0.0.1:5432/akeno \
PUBLIC_ORIGIN=https://akeno.example.com \
WEB_DIST_DIR=$PWD/apps/web/dist \
MIGRATE_ON_START=true \
node apps/server/dist/main.js
```

The server bundle in `apps/server/dist` still needs the server's production dependencies
(`pnpm --filter @akeno/server deploy --prod --legacy <dir>` assembles a standalone folder).
Docker Compose ([VPS_DEPLOYMENT.md](VPS_DEPLOYMENT.md)) is the supported production path.

## Configuration reference

All settings are environment variables validated at startup (`apps/server/src/config/env.ts`).
Invalid values stop the server with an English error message.

| Variable | Default | Meaning |
| --- | --- | --- |
| `NODE_ENV` | `development` | `production` enables production checks |
| `HOST` / `PORT` | `127.0.0.1` / `3000` | Listen address (Docker uses `0.0.0.0`) |
| `DATABASE_URL` | — (required) | PostgreSQL connection string |
| `DB_POOL_MAX` | `10` | Maximum database connections |
| `PUBLIC_ORIGIN` | — (required) | Exact origin users open in the browser |
| `ADDITIONAL_ORIGINS` | empty | Extra allowed origins, comma separated |
| `TRUST_PROXY` | `false` | Trusted proxy addresses (e.g. `loopback,uniquelocal`); hop counts are rejected |
| `REGISTRATION_MODE` | `invite` | `open` is refused in production unless `ALLOW_OPEN_REGISTRATION_IN_PRODUCTION=true` |
| `SESSION_TTL_DAYS` / `SESSION_IDLE_TIMEOUT_DAYS` | `30` / `7` | Absolute and idle session lifetime |
| `COOKIE_SECURE` | `auto` | Secure cookies when `PUBLIC_ORIGIN` is HTTPS; `false` is refused in production |
| `ARGON2_MEMORY_KIB`, `ARGON2_TIME_COST`, `ARGON2_PARALLELISM` | `19456`, `2`, `1` | Argon2id parameters |
| `ARGON2_MAX_CONCURRENCY` | `4` | Concurrent hashes (bounds memory use) |
| `RATE_LIMIT_ENABLED`, `RATE_LIMIT_MAX_PER_MINUTE` | `true`, `300` | HTTP rate limiting |
| `WEB_DIST_DIR` | empty | Serve the built web client from this directory |
| `MIGRATE_ON_START` | `false` | Apply migrations on startup |
| `LOG_LEVEL`, `LOG_PRETTY` | `info`, dev only | Logging |
