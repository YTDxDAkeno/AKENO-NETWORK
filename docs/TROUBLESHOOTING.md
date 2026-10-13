# Troubleshooting

Start with the logs: `docker compose logs --since 30m app` (Docker) or the terminal running
`pnpm dev`. Every API error response contains a `requestId` that also appears in the server
log line for that request.

## Startup

| Symptom | Cause and fix |
| --- | --- |
| `Invalid AKENO NETWORK configuration: - DATABASE_URL: …` | A required variable is missing or malformed. The message names it. Check `.env` (development) or `compose.yaml`/`.env` (Docker). |
| `PUBLIC_ORIGIN must be a full URL` | Use `https://akeno.example.com` or `http://localhost:5173` — scheme and host, no path. |
| `Open registration is disabled in production` | Use `REGISTRATION_MODE=invite`, or set `ALLOW_OPEN_REGISTRATION_IN_PRODUCTION=true` deliberately. |
| `TRUST_PROXY hop counts are not supported` | Use addresses or names, e.g. `TRUST_PROXY=loopback,uniquelocal`. |
| `ECONNREFUSED 127.0.0.1:5432` | PostgreSQL is not running or listens elsewhere. `docker compose -f compose.dev.yaml up -d` or fix `DATABASE_URL`. |
| `password authentication failed for user "akeno"` | `DATABASE_URL` and the database password differ. For Docker, `POSTGRES_PASSWORD` only applies when the volume is first created; changing it later requires `ALTER USER akeno PASSWORD '…'` inside the database. |
| `/health/ready` returns 503 `migrations: pending` | Run `pnpm db:migrate` (development) or set `MIGRATE_ON_START=true` (Docker default). |
| Container `app` stays `unhealthy` | `docker compose logs app`. Usually database credentials or a migration error. |
| Docker build fails with `SELF_SIGNED_CERT_IN_CHAIN` | You are behind a TLS-intercepting proxy. Pass its CA: `docker build --secret id=extra_ca,src=/path/ca.crt …` (see the Dockerfile header). |
| Docker build is killed on a 1–2 GB VPS | Add swap (see VPS_DEPLOYMENT.md) or build the image elsewhere and `docker save`/`docker load` it. |
| Caddy cannot obtain a certificate | DNS must point to the server, ports 80 and 443 must be open, and `AKENO_DOMAIN` must be set. `docker compose logs caddy` shows the ACME error. Let's Encrypt rate-limits repeated failures; fix DNS first, then restart Caddy. |

## Signing in and requests

| Symptom | Cause and fix |
| --- | --- |
| "This request was blocked because it did not come from the AKENO NETWORK app." (`ORIGIN_REJECTED`) | The address in the browser does not match `PUBLIC_ORIGIN`/`AKENO_PUBLIC_ORIGIN` exactly (including `http` vs `https`, `localhost` vs `127.0.0.1`, and the port). Use the configured address or add it to `ADDITIONAL_ORIGINS`. |
| "Your security token is missing or out of date" (`CSRF_FAILED`) | Usually after signing in on another tab. The client refreshes the token automatically once; reload the page if it persists. |
| Signed out immediately after signing in over plain HTTP | A Secure cookie cannot be stored over HTTP. Do not set `COOKIE_SECURE=true` without HTTPS. |
| "Too many failed sign-in attempts. Try again in N minutes." | Account lockout after repeated wrong passwords. Wait, or restart the app to clear the in-memory throttle (operators only). |
| "Too many requests" for several users behind one router | Unauthenticated limits are per IP. If everyone shares one public IP and many sign in at once, increase `RATE_LIMIT_MAX_PER_MINUTE`. Behind Caddy, make sure `TRUST_PROXY=loopback,uniquelocal` so the real client IP is used. |
| Forgot the admin password | `docker compose exec app node dist/cli.js reset-password --online-id <id>` |
| Lost access to every admin account | `node dist/cli.js promote --online-id <id>` on the server grants admin rights. |
| Invitation code "invalid, used up or expired" | Check the code in Administration → Invitations (status column). Codes are case-insensitive; dashes are optional. |

## Realtime

| Symptom | Cause and fix |
| --- | --- |
| Header shows **Reconnecting…** permanently | The WebSocket at `/ws` is blocked. Behind Caddy it works out of the box; behind another proxy, enable WebSocket upgrades (`Upgrade`/`Connection` headers) and long idle timeouts. Check the browser console for the close code. |
| WebSocket closes with code 4401 | The session was revoked or expired; sign in again. |
| Friends always appear offline | Presence is only shared according to their privacy settings (default: friends only). Users set to **Invisible** appear offline. Blocked users always appear offline. |
| Messages appear only after reloading | The realtime connection is down (see above). Messages are always stored; the client catches up automatically after reconnecting. |
| Typing indicator never shows | It is only sent while typing and is throttled to every 3 seconds; both users must be connected. |

## Development

| Symptom | Cause and fix |
| --- | --- |
| `pnpm dev`: port 5173 or 3000 in use | Another process uses the port. Stop it or change `PORT` / Vite's port. |
| Server tests fail with `permission denied to create database` | The test role needs `CREATEDB`. Set `TEST_DATABASE_ADMIN_URL` to a suitable role (see TESTING.md). |
| Playwright cannot find a browser | `pnpm --filter @akeno/web exec playwright install chromium` |
| E2E: `Missing apps/server/dist/main.js` | Run `pnpm build` first (`pnpm test:e2e` does this automatically). |
| Drizzle schema changed but nothing happens | Generate a migration with `pnpm db:generate`, review it, then `pnpm db:migrate`. |

## Data

| Symptom | Cause and fix |
| --- | --- |
| Need to undo a mistake | Restore the latest backup: `./infrastructure/scripts/restore.sh backups/<file>.dump`. |
| Database volume deleted | Only recoverable from a backup. Never use `docker compose down -v` in production. |
| Disk full | `docker system df`; prune old images (`docker image prune`), rotate backups (`KEEP=` in backup.sh), check `docker compose logs` sizes (limited to 3 × 10 MB per container). |

## Getting more detail

- Set `LOG_LEVEL=debug` temporarily (Docker: add it to `.env`, then `docker compose up -d`).
- `GET /health/ready` shows database and migration state.
- **Administration → System** shows live connections, memory and the database pool.
- **Administration → Audit log** shows sign-ins, failures and administrative actions.
