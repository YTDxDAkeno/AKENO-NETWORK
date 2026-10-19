# VPS deployment (Ubuntu / Debian)

This guide deploys AKENO NETWORK on a small Linux VPS with Docker Compose and automatic HTTPS
through Caddy.

```
 Browser / web client
        │  HTTPS (443) + WSS
        ▼
   Caddy (container) ── automatic Let's Encrypt certificates, HTTP→HTTPS redirect
        │  private Docker network
        ▼
   AKENO app (container) ── API, WebSockets, web client
        │  internal network (no internet route)
        ▼
   PostgreSQL 16 (container) ── persistent volume, never published
```

## Sizing

| Plan | Status |
| --- | --- |
| 1 vCPU, 2 GB RAM, 20 GB SSD | Primary target. A single-core approximation handled 20 simulated users with p95 message delivery of 67 ms; see [PERFORMANCE.md](PERFORMANCE.md). Not yet verified on real VPS hardware. |
| 2 vCPU, 4 GB RAM | Recommended for headroom (builds are faster, and backups/upgrades do not compete with traffic) |

Building the Docker image on a 1 vCPU / 2 GB VPS works but takes several minutes; add swap if
the build is killed for lack of memory (`fallocate -l 2G /swapfile && chmod 600 /swapfile &&
mkswap /swapfile && swapon /swapfile`, plus an `/etc/fstab` entry).

## What DNS does — and does not do

DNS only maps a name such as `akeno.example.com` to your server's IP address (A record for
IPv4, AAAA for IPv6). It is required so browsers can find the server and so Let's Encrypt can
issue a certificate.

DNS does **not** provide user registration, authentication, friends, chat, matchmaking, NAT
traversal or any game multiplayer protocol. All of those are implemented by the AKENO NETWORK
application itself. Never redirect Sony or other vendor domains to this server: it would not
add any compatibility and it is not supported.

## 1. Prepare the server

Log in as a user with `sudo`:

```bash
sudo apt update && sudo apt upgrade -y
sudo apt install -y ca-certificates curl git ufw
```

Install Docker Engine and the Compose plugin from Docker's official repository
(<https://docs.docker.com/engine/install/ubuntu/> or `/debian/`). For Ubuntu:

```bash
sudo install -m 0755 -d /etc/apt/keyrings
sudo curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] https://download.docker.com/linux/ubuntu $(. /etc/os-release && echo "$VERSION_CODENAME") stable" | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null
sudo apt update
sudo apt install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
sudo usermod -aG docker "$USER"   # log out and back in afterwards
docker compose version
```

## 2. Firewall

Expose only SSH, HTTP and HTTPS. PostgreSQL and the app port are never public.

```bash
sudo ufw default deny incoming
sudo ufw default allow outgoing
sudo ufw allow OpenSSH
sudo ufw allow 80/tcp
sudo ufw allow 443/tcp
sudo ufw allow 443/udp     # HTTP/3 (optional)
sudo ufw enable
sudo ufw status verbose
```

Party voice (optional, Alpha 0.4) additionally needs **7881/tcp and 7882/udp** open to the
internet once you enable the `voice` profile — see [VOICE.md](VOICE.md#deploying-the-voice-server).
Leave them closed until then.

Note: Docker publishes ports through its own iptables rules, which bypass `ufw`. That is why
`compose.yaml` binds the app to `127.0.0.1` and does not publish PostgreSQL at all. Keep
`APP_BIND_ADDRESS=127.0.0.1` on a VPS. Also disable SSH password logins if you use keys
(`PasswordAuthentication no` in `/etc/ssh/sshd_config`).

## 3. DNS records

At your DNS provider create:

| Type | Name | Value |
| --- | --- | --- |
| A | `akeno` (or `@`) | your VPS IPv4 address |
| AAAA | `akeno` (optional) | your VPS IPv6 address |

Wait until `dig +short akeno.example.com` returns your IP before starting Caddy.

## 4. Get the code and configure secrets

```bash
sudo mkdir -p /opt/akeno && sudo chown "$USER" /opt/akeno
git clone <your repository URL> /opt/akeno
cd /opt/akeno
cp .env.example .env
chmod 600 .env
nano .env
```

Set at least:

```ini
POSTGRES_PASSWORD=<output of: openssl rand -base64 36>
AKENO_DOMAIN=akeno.example.com
AKENO_PUBLIC_ORIGIN=https://akeno.example.com
APP_BIND_ADDRESS=127.0.0.1
REGISTRATION_MODE=invite
```

`AKENO_PUBLIC_ORIGIN` must be exactly the URL users type (scheme + host, no trailing slash);
it is used for CSRF/origin checks, WebSocket origin checks and secure cookies. The other
variables in `.env.example` under "Local development" are not used by Compose.

## 5. Start

```bash
docker compose --profile https up -d --build
docker compose --profile https ps
docker compose logs -f app caddy        # Ctrl+C stops following
```

The app applies database migrations on start (`MIGRATE_ON_START=true` in the image). When
`app` is `healthy` and Caddy has obtained a certificate, open `https://akeno.example.com`.

## 6. Bootstrap the first administrator

No credentials are built in. Create the first administrator from the server shell:

```bash
docker compose exec app node dist/cli.js create-admin --online-id <your-id>
docker compose exec app node dist/cli.js create-invitation --max-uses 1 --expires-in-hours 72 --note "For Bob"
```

Sign in, then manage invitation codes in **Administration → Invitations**. Codes are shown
once and stored only as hashes. Send them through a private channel.

Other operator commands: `promote --online-id <id>` (grant admin), `reset-password
--online-id <id>` (sets a new password and signs the account out everywhere), `migrate`.

## Operations

| Task | Command (in `/opt/akeno`) |
| --- | --- |
| Status | `docker compose --profile https ps` |
| Logs | `docker compose logs --since 1h app` (JSON; rotated at 3 × 10 MB per container) |
| Stop | `docker compose --profile https stop` |
| Start | `docker compose --profile https start` |
| Restart the app | `docker compose restart app` (clients reconnect automatically) |
| Shell in the database | `docker compose exec db psql -U akeno -d akeno` |

Always use `--profile https` with `up`/`stop`/`start` so Caddy is included. If party voice is
enabled, add `--profile voice` to the same commands (LiveKit logs: `docker compose logs livekit`).

### Upgrade

```bash
cd /opt/akeno
./infrastructure/scripts/backup.sh        # always back up first
git pull
docker compose --profile https up -d --build
docker compose --profile https ps
```

Migrations run automatically and are transactional. Never run `docker compose down -v`
during upgrades: `-v` deletes the database volume.

When an upgrade changes `infrastructure/caddy/Caddyfile`, recreate Caddy
(`docker compose --profile https up -d --no-deps --force-recreate caddy`): the file is
bind-mounted on its own, and after `git pull` replaced it, `caddy reload` still reads the old one.
The console voice upgrade has its own procedure and scripts:
[SERVER_VOICE_UPGRADE.md](SERVER_VOICE_UPGRADE.md).

### Backups

```bash
./infrastructure/scripts/backup.sh                 # → ./backups/akeno-<stamp>.dump + akeno-media-<stamp>.tar.gz
BACKUP_DIR=/var/backups/akeno KEEP=30 ./infrastructure/scripts/backup.sh
```

Since Alpha 0.3 profiles can contain uploaded pictures and banners, stored on the `media-data`
volume (not in PostgreSQL). **A database dump alone is no longer a complete backup**: keep the
dump and the media archive of the same run together. The script dumps the database first and
then archives the media volume, so the archive contains every image the dump refers to.

Automate daily backups with cron (`crontab -e`):

```cron
15 3 * * * cd /opt/akeno && BACKUP_DIR=/var/backups/akeno ./infrastructure/scripts/backup.sh >> /var/log/akeno-backup.log 2>&1
```

Copy backups off the server regularly (for example with `rsync` or your provider's
object storage). A backup that only exists on the same VPS does not protect against losing
the VPS. Backups contain private messages — store them encrypted and with restricted access.

### Restore

```bash
./infrastructure/scripts/restore.sh backups/akeno-20261013T031500Z.dump backups/akeno-media-20261013T031500Z.tar.gz
```

The script asks for confirmation, stops the app, restores the database in a single transaction,
replaces the media files (unpacked next to the old ones, then swapped) and starts the app again.
Without a media archive only the database is restored; pictures whose files are missing then
fall back to preset avatars. Restoring onto a new server: install as above, start the stack once, then run
the restore script.

### Monitoring

- `GET https://akeno.example.com/health/ready` returns 200 when the app and database are
  healthy — suitable for an external uptime monitor.
- Administrators can see live connections, memory and database pool usage under
  **Administration → System**.

## Security checklist

- [ ] `.env` has a strong, unique `POSTGRES_PASSWORD` and `chmod 600`
- [ ] `AKENO_PUBLIC_ORIGIN` uses `https://`
- [ ] Only ports 22, 80 and 443 are reachable (`sudo ss -tlnp`, external port scan) — plus
      7881/tcp and 7882/udp only if party voice is enabled; 7880 is never public
- [ ] `REGISTRATION_MODE=invite`
- [ ] Daily backups copied off the server; a restore has been tested
- [ ] OS security updates enabled (`sudo apt install unattended-upgrades`)

See [SECURITY.md](SECURITY.md) for the full security model and
[TROUBLESHOOTING.md](TROUBLESHOOTING.md) for common problems.
