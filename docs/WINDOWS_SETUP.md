# Windows 11 setup

Two ways to run AKENO NETWORK on Windows 11:

- **A. Docker Desktop only** — run the production stack locally. No Node.js needed. Best for
  hosting a small private network from your PC or trying it out.
- **B. Development** — Node.js + pnpm with PostgreSQL in Docker. Best for changing the code.

Commands below are for **PowerShell**. Run them from the repository folder.

## Prerequisites

1. **Docker Desktop for Windows** with the WSL 2 backend (default). Start it once and wait
   until it reports "Engine running".
2. **Git for Windows** to clone the repository.
3. For option B only: **Node.js 22 LTS** (from nodejs.org) and pnpm:
   ```powershell
   npm install --global pnpm@10
   ```

Clone the repository:

```powershell
git clone <your repository URL> akeno-network
cd akeno-network
```

## A. Run the production stack with Docker Desktop

1. Create the configuration file:
   ```powershell
   Copy-Item .env.example .env
   notepad .env
   ```
   Set at least:
   ```ini
   POSTGRES_PASSWORD=<a long random password>
   AKENO_PUBLIC_ORIGIN=http://localhost:8080
   ```
   To generate a random password in PowerShell:
   ```powershell
   -join ((48..57) + (65..90) + (97..122) | Get-Random -Count 32 | ForEach-Object { [char]$_ })
   ```

2. Build and start:
   ```powershell
   docker compose up -d --build
   docker compose ps
   ```
   Wait until `app` shows `(healthy)`.

3. Create the first administrator and an invitation code:
   ```powershell
   docker compose exec app node dist/cli.js create-admin --online-id admin
   docker compose exec app node dist/cli.js create-invitation --max-uses 5
   ```
   The first command asks for a password (at least 12 characters).

4. Open <http://localhost:8080> and sign in.

### Letting friends on your home network connect

By default the app only listens on `127.0.0.1` (this PC). To allow devices on your LAN:

1. In `.env` set `APP_BIND_ADDRESS=0.0.0.0` and
   `ADDITIONAL_ORIGINS=http://<your-PC-IP>:8080` (find the IP with `ipconfig`).
2. `docker compose up -d` to apply.
3. Allow inbound TCP 8080 in Windows Defender Firewall for **private** networks only:
   ```powershell
   New-NetFirewallRule -DisplayName "AKENO NETWORK (LAN)" -Direction Inbound -Protocol TCP -LocalPort 8080 -Profile Private -Action Allow
   ```

Do **not** expose port 8080 directly to the internet. For internet access use a VPS with
HTTPS ([VPS_DEPLOYMENT.md](VPS_DEPLOYMENT.md)).

### Day-to-day operation

```powershell
docker compose stop          # stop (data is kept)
docker compose start         # start again
docker compose logs -f app   # follow logs (Ctrl+C to stop following)
git pull; docker compose up -d --build   # update to the latest code (data is kept)
docker compose pull db                    # refresh the PostgreSQL image within version 16
```

### Backups on Windows

The backup scripts are Bash scripts. In PowerShell, use the equivalent commands (they avoid
PowerShell redirection, which would corrupt binary dumps):

```powershell
New-Item -ItemType Directory -Force backups | Out-Null
$stamp = Get-Date -Format "yyyyMMddTHHmmss"
docker compose exec -T db pg_dump -U akeno -d akeno --format=custom --no-owner --file /tmp/akeno.dump
docker compose cp db:/tmp/akeno.dump "backups/akeno-$stamp.dump"
```

Restore (replaces all data):

```powershell
docker compose stop app
docker compose cp backups\akeno-<timestamp>.dump db:/tmp/restore.dump
docker compose exec -T db pg_restore -U akeno -d akeno --clean --if-exists --no-owner --single-transaction /tmp/restore.dump
docker compose start app
```

Alternatively run `infrastructure/scripts/backup.sh` from Git Bash or WSL.

## B. Development setup

1. Start PostgreSQL for development:
   ```powershell
   docker compose -f compose.dev.yaml up -d
   ```
2. Configure:
   ```powershell
   Copy-Item .env.example .env
   ```
   The defaults match the development database (`DATABASE_URL` uses
   `change-me-local-dev-password`; change both together if you change it).
3. Install and prepare:
   ```powershell
   pnpm install
   pnpm db:migrate
   pnpm admin:create --online-id admin
   pnpm invite:create --max-uses 5
   ```
   If `admin:create` cannot prompt for a password in your terminal, pipe it in:
   `"your-admin-password" | pnpm admin:create --online-id admin --password-stdin`.
4. Run:
   ```powershell
   pnpm dev
   ```
   Open <http://localhost:5173>.

### Running the tests on Windows

The server integration tests need a PostgreSQL role allowed to create databases. With the
development container, the `akeno` user is a superuser, so set:

```powershell
$env:TEST_DATABASE_ADMIN_URL = "postgres://akeno:change-me-local-dev-password@127.0.0.1:5432/postgres"
$env:E2E_DATABASE_ADMIN_URL = $env:TEST_DATABASE_ADMIN_URL
pnpm test
pnpm --filter @akeno/web exec playwright install chromium
pnpm test:e2e
```

## Windows troubleshooting

| Symptom | Fix |
| --- | --- |
| `docker: command not found` / pipe errors | Start Docker Desktop and wait for "Engine running" |
| Port 5432 or 8080 already in use | Stop the other program, or change `APP_PORT` in `.env` / the port mapping in `compose.dev.yaml` |
| Sign-in works but every action says "did not come from the AKENO NETWORK app" | The browser URL does not match `AKENO_PUBLIC_ORIGIN` / `PUBLIC_ORIGIN`. Use exactly the configured address (including port) or add it to `ADDITIONAL_ORIGINS` |
| `pnpm` blocked by execution policy | Run `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned` once |
| `$'\r': command not found` when running a `.sh` script | The file was checked out with Windows line endings. The repository's `.gitattributes` forces LF; run `git rm --cached -r . ; git reset --hard` once to re-checkout |

More in [TROUBLESHOOTING.md](TROUBLESHOOTING.md).
