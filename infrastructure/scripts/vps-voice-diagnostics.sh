#!/usr/bin/env bash
# READ-ONLY diagnostics of an AKENO NETWORK deployment before the console voice upgrade
# (docs/SERVER_VOICE_UPGRADE.md, step 0). It changes nothing: no git fetch/checkout, no container
# start/stop, no file writes except its own report, no database access, no firewall change.
#
#   cd /opt/akeno && bash vps-voice-diagnostics.sh            # report on stdout
#   cd /opt/akeno && bash vps-voice-diagnostics.sh > ~/akeno-diagnostics.txt
#
# Secrets are never printed: of .env only the names of the keys are shown, plus the values of a
# fixed list of public, non-secret settings (domain, public URLs, LiveKit node IP). Remote URLs
# are printed without credentials. Read this script before running it.
set -uo pipefail

AKENO_DIR="${AKENO_DIR:-$(pwd)}"
cd "$AKENO_DIR" || { echo "No such directory: $AKENO_DIR" >&2; exit 1; }
[[ -f compose.yaml ]] || { echo "Run this in the AKENO checkout (compose.yaml not found in $AKENO_DIR)." >&2; exit 1; }

section() { printf '\n=== %s ===\n' "$1"; }
run() { printf '$ %s\n' "$*"; "$@" 2>&1 | sed 's/^/  /'; }
# Public settings whose values may be shown; everything else in .env is shown by name only.
PUBLIC_KEYS='^(AKENO_DOMAIN|AKENO_PUBLIC_ORIGIN|ADDITIONAL_ORIGINS|REGISTRATION_MODE|LIVEKIT_URL|LIVEKIT_API_URL|LIVEKIT_NODE_IP|LIVEKIT_USE_EXTERNAL_IP|LIVEKIT_BIND_ADDRESS|VOICE_TOKEN_TTL_SECONDS|VOICE_GATEWAY_URL|VOICE_GATEWAY_MAX_SESSIONS|APP_BIND_ADDRESS|APP_PORT|LOG_LEVEL|COMPOSE_PROFILES|CADDY_IMAGE|LIVEKIT_IMAGE|POSTGRES_IMAGE|NODE_IMAGE)$'
envval() { [[ -f .env ]] && grep -E "^$1=" .env | tail -1 | cut -d= -f2- | tr -d '"'"'"; }

echo "AKENO NETWORK voice upgrade diagnostics (read-only) — $(date -u +%Y-%m-%dT%H:%M:%SZ) on $(hostname)"
echo "Directory: $AKENO_DIR"

section "1. Git: deployed commit, branch, unpublished commits, local changes"
run git rev-parse HEAD
run git log -1 --format='%H %ci %s'
run git branch -vv --no-color
printf '$ git remote -v (credentials removed)\n'
git remote -v 2>&1 | sed -E 's#(https?://)[^/@]+@#\1#' | sed 's/^/  /'
run git log --oneline -25 --no-color
echo "Commits not contained in any remote-tracking branch known to this checkout (candidates for VPS-only work):"
git log --oneline --no-color HEAD --not --remotes 2>&1 | sed 's/^/  /'
echo "Local branches and tags pointing at HEAD:"
git branch --points-at HEAD --no-color 2>&1 | sed 's/^/  /'
git tag --points-at HEAD 2>&1 | sed 's/^/  /'
run git status --porcelain=v1 --untracked-files=normal
run git diff --stat
echo "Uncommitted changes to deployment files (these files contain no secrets, only \${VAR} references):"
git diff -- compose.yaml infrastructure/caddy/Caddyfile infrastructure/docker 2>&1 | sed 's/^/  /'
echo "Is the deployed commit on GitHub? (read-only query of the public repository)"
if remote_heads="$(git ls-remote https://github.com/YTDxDAkeno/AKENO-NETWORK 2>/dev/null)"; then
  head_sha="$(git rev-parse HEAD)"
  if grep -q "^${head_sha}" <<<"$remote_heads"; then echo "  HEAD is the tip of: $(grep "^${head_sha}" <<<"$remote_heads" | awk '{print $2}' | tr '\n' ' ')"
  else echo "  HEAD ${head_sha} is not the tip of any GitHub branch (it may still be inside one; compare on GitHub)."; fi
else echo "  (GitHub not reachable from here; skipped)"; fi

section "2. Server code on this checkout: which voice features exist"
for f in apps/server/src/modules/voice/routes.ts apps/server/src/modules/voice/service.ts apps/voice-gateway/package.json infrastructure/docker/voice-gateway.Dockerfile; do
  if [[ -f "$f" ]]; then echo "  present: $f"; else echo "  missing: $f"; fi
done
if grep -q "voice/device-token" apps/server/src/modules/voice/routes.ts 2>/dev/null; then echo "  console voice pass route: in the source"; else echo "  console voice pass route: NOT in the source (explains HTTP 404)"; fi
echo "  migrations: $(find apps/server/src/database/migrations -maxdepth 1 -name '*.sql' -printf '%f ' 2>/dev/null)"
echo "  Caddyfile routes:"; grep -nE 'handle(_path)? ' infrastructure/caddy/Caddyfile 2>/dev/null | sed 's/^/    /'

section "3. Configuration (.env): key names; values only for public settings"
if [[ -f .env ]]; then
  while IFS= read -r line; do
    [[ "$line" =~ ^[A-Z0-9_]+= ]] || continue
    key="${line%%=*}"
    if [[ "$key" =~ $PUBLIC_KEYS ]]; then echo "  $key=$(envval "$key")"
    elif [[ -n "${line#*=}" ]]; then echo "  $key=<set, hidden>"
    else echo "  $key=<empty>"; fi
  done < .env
else echo "  no .env file in $AKENO_DIR"; fi

section "4. Docker: versions, running services, images"
run docker --version
run docker compose version
run docker compose --profile https --profile voice config --services
run docker compose --profile https --profile voice ps --format 'table {{.Service}}\t{{.Image}}\t{{.Status}}\t{{.CreatedAt}}'
echo "Images of the running containers:"
for c in $(docker compose --profile https --profile voice ps -q 2>/dev/null); do
  docker inspect --format '  {{.Name}}  image={{.Config.Image}}  id={{.Image}}  started={{.State.StartedAt}}  restarts={{.RestartCount}}' "$c" 2>&1
done
run docker image ls --format 'table {{.Repository}}:{{.Tag}}\t{{.ID}}\t{{.CreatedAt}}\t{{.Size}}' akeno-network
run docker image ls --format 'table {{.Repository}}:{{.Tag}}\t{{.ID}}\t{{.CreatedAt}}' akeno-voice-gateway

section "5. Health, and whether the console voice pass route exists (unauthenticated probes, no data)"
domain="$(envval AKENO_DOMAIN)"
if [[ -n "$domain" ]]; then
  printf '$ GET https://%s/health/ready\n  ' "$domain"; curl -s -o /dev/null -w '%{http_code}\n' --max-time 10 "https://$domain/health/ready"
  # With an invalid device token, as a console sends it: 401 UNAUTHENTICATED = the route exists
  # (upgraded server); 404 NOT_FOUND = the route does not exist (the old server, the PS5's error).
  printf '$ POST https://%s/api/v1/parties/<zero id>/voice/device-token (invalid device token)\n  ' "$domain"
  curl -s --max-time 10 -X POST -H 'Authorization: Bearer akd_invalid-diagnostics-probe' -H 'Content-Type: application/json' -d '{}' \
    "https://$domain/api/v1/parties/00000000-0000-4000-8000-000000000000/voice/device-token" | head -c 300; echo
  printf '$ GET https://%s/livekit/ (LiveKit signalling through Caddy)\n  ' "$domain"; curl -s -o /dev/null -w '%{http_code}\n' --max-time 10 "https://$domain/livekit/"
  printf '$ WebSocket upgrade https://%s/voice-gateway (101 = gateway reachable; 200/other = not routed)\n  ' "$domain"
  curl -s -o /dev/null -w '%{http_code}\n' --http1.1 --max-time 4 -H 'Connection: Upgrade' -H 'Upgrade: websocket' -H 'Sec-WebSocket-Version: 13' \
    -H "Sec-WebSocket-Key: $(head -c 16 /dev/urandom | base64)" "https://$domain/voice-gateway"
else echo "  AKENO_DOMAIN not set in .env; skipped"; fi
if docker compose --profile voice ps -q livekit 2>/dev/null | grep -q .; then echo "  LiveKit container: running (browser voice is set up)"; else echo "  LiveKit container: NOT running (browser voice is off)"; fi
if docker compose --profile voice ps -q voice-gateway 2>/dev/null | grep -q .; then
  printf '  voice gateway /healthz: '; docker compose --profile voice exec -T voice-gateway node -e "fetch('http://127.0.0.1:7890/healthz').then(r=>r.text()).then(t=>console.log(t)).catch(e=>console.log(String(e)))" 2>&1
else echo "  voice gateway container: not running"; fi

section "6. LiveKit media networking (the gateway reaches LiveKit's media port through this address)"
echo "  LIVEKIT_NODE_IP=$(envval LIVEKIT_NODE_IP)  LIVEKIT_USE_EXTERNAL_IP=$(envval LIVEKIT_USE_EXTERNAL_IP)"
echo "  IPv4 addresses on this machine's interfaces:"; ip -4 -o addr show scope global 2>/dev/null | awk '{print "    " $2 " " $4}'
[[ -n "$domain" ]] && echo "  $domain resolves to: $(getent ahostsv4 "$domain" 2>/dev/null | awk '{print $1}' | sort -u | tr '\n' ' ')"
echo "  Listening sockets for 80, 443, 7880-7882:"
(ss -Htulpn 2>/dev/null || ss -Htuln 2>/dev/null) | awk '$5 ~ /:(80|443|7880|7881|7882)$/ {print "    " $1 " " $5}'

section "7. Firewall (read-only status; may need sudo, skipped otherwise)"
if command -v ufw >/dev/null 2>&1; then (sudo -n ufw status verbose 2>/dev/null || ufw status 2>/dev/null || echo "  ufw present; status needs sudo") | sed 's/^/  /'; else echo "  ufw not installed"; fi

section "8. Capacity"
run nproc
run free -m
run df -h "$AKENO_DIR" /var/lib/docker
run uptime
printf '$ docker stats --no-stream\n'; docker stats --no-stream --format '  {{.Name}}  cpu={{.CPUPerc}}  mem={{.MemUsage}}' 2>&1

section "9. Backups"
for d in ./backups "${BACKUP_DIR:-}" /var/backups/akeno; do
  [[ -n "$d" && -d "$d" ]] || continue
  echo "  $d (newest 6):"; ls -1t "$d" 2>/dev/null | head -6 | sed 's/^/    /'
done
echo "  cron entries mentioning backup.sh:"; (crontab -l 2>/dev/null | grep -n 'backup.sh' || echo "    none for $(whoami)") | sed 's/^/  /'

section "10. Recent server errors (last 24 h, error level only, at most 20 lines)"
docker compose logs --no-color --since 24h app 2>/dev/null | grep -E '"level":(50|60)' | tail -20 | cut -c1-400 | sed 's/^/  /'
echo
echo "End of report. It contains no passwords, keys or tokens; check before sharing anyway."
