#!/usr/bin/env bash
# Console voice server upgrade and rollback on the VPS (docs/SERVER_VOICE_UPGRADE.md).
# Run it only after the owner approved the deployment, in the AKENO checkout (/opt/akeno),
# AFTER the code update (git merge --ff-only of the reviewed commit). Every command that changes
# something asks for a typed confirmation first.
#
#   ./infrastructure/scripts/voice-upgrade.sh preflight       read-only checks (blockers exit 1)
#   ./infrastructure/scripts/voice-upgrade.sh build           build the app and gateway images; the
#                                                             running containers are not touched
#   ./infrastructure/scripts/voice-upgrade.sh apply           start the gateway, set VOICE_GATEWAY_URL,
#                                                             recreate the app, recreate Caddy, verify
#   ./infrastructure/scripts/voice-upgrade.sh verify          read-only checks after the upgrade
#   ./infrastructure/scripts/voice-upgrade.sh rollback 1|2|3  1 consoles off, 2 gateway stopped,
#                                                             3 previous commit, image, .env, Caddy
#
# Only the app, the new voice-gateway and Caddy are (re)created; PostgreSQL and LiveKit are never
# restarted. No database migration is involved. The rollback point (previous image tag, .env copy,
# previous commit) must exist before `apply`: see the guide, step 2.
set -euo pipefail

AKENO_DIR="${AKENO_DIR:-$(pwd)}"
cd "$AKENO_DIR"
[[ -f compose.yaml ]] || { echo "Run this in the AKENO checkout (compose.yaml not found)." >&2; exit 1; }
ROLLBACK_TAG="${ROLLBACK_TAG:-akeno-network:pre-voice}"
ROLLBACK_ENV="${ROLLBACK_ENV:-.env.pre-voice}"
ROLLBACK_COMMIT_FILE="${ROLLBACK_COMMIT_FILE:-.pre-voice-commit}"
COMPOSE=(docker compose --profile https --profile voice)

say() { printf '\n== %s\n' "$*"; }
fail() { printf 'BLOCKER: %s\n' "$*" >&2; blockers=$((blockers + 1)); }
envval() { grep -E "^$1=" .env 2>/dev/null | tail -1 | cut -d= -f2- | tr -d '"'"'"; }
confirm() {
  [[ "${ASSUME_YES:-}" == "1" ]] && return 0
  read -r -p "$1 Type YES to continue: " answer
  [[ "$answer" == "YES" ]] || { echo "Cancelled."; exit 1; }
}
cid() { "${COMPOSE[@]}" ps -q "$1" 2>/dev/null; }
wait_healthy() {
  local service="$1" limit="${2:-120}" id state
  for _ in $(seq 1 "$limit"); do
    id="$(cid "$service")"
    state="$( [[ -n "$id" ]] && docker inspect --format '{{if .State.Health}}{{.State.Health.Status}}{{else}}{{.State.Status}}{{end}}' "$id" 2>/dev/null)"
    [[ "$state" == "healthy" || "$state" == "running" && "$service" == "caddy" ]] && { echo "  $service: $state"; return 0; }
    sleep 1
  done
  echo "  $service did not become healthy (last state: ${state:-none})" >&2
  return 1
}
set_env() { # set_env KEY VALUE: replaces or appends one line, keeps a copy of the previous .env
  local key="$1" value="$2" tmp
  cp -p .env ".env.before-$key-$(date -u +%Y%m%dT%H%M%SZ)"
  tmp="$(mktemp .env.XXXXXX)"
  grep -vE "^$key=" .env > "$tmp" || true
  printf '%s=%s\n' "$key" "$value" >> "$tmp"
  chmod --reference=.env "$tmp" 2>/dev/null || chmod 600 "$tmp"
  mv "$tmp" .env
}
# Probes print an HTTP status; 000 means no answer (DNS, TLS or connection failure) and is
# reported, never fatal, so verify always shows every check.
http_status() { curl -s -o /dev/null -w '%{http_code}' --max-time 10 "$@" || true; }
probe_route() { # voice pass request with an invalid device token, as a console sends it: 401 = route exists, 404 = missing
  http_status -X POST -H 'Authorization: Bearer akd_invalid-diagnostics-probe' -H 'Content-Type: application/json' -d '{}' \
    "https://$1/api/v1/parties/00000000-0000-4000-8000-000000000000/voice/device-token"
}
probe_ws() { # 101 = the voice gateway answered the WebSocket upgrade through Caddy
  curl -s -o /dev/null -w '%{http_code}' --http1.1 --max-time 4 -H 'Connection: Upgrade' -H 'Upgrade: websocket' \
    -H 'Sec-WebSocket-Version: 13' -H "Sec-WebSocket-Key: $(head -c 16 /dev/urandom | base64)" "https://$1/voice-gateway" || true
}

domain="$(envval AKENO_DOMAIN)"
blockers=0

preflight() {
  say "Preflight (read-only)"
  [[ -n "$domain" ]] || fail "AKENO_DOMAIN is not set in .env"
  [[ -z "$(git status --porcelain --untracked-files=no)" ]] || fail "the checkout has uncommitted changes to tracked files (git status); publish or keep them first"
  [[ -f apps/voice-gateway/package.json && -f infrastructure/docker/voice-gateway.Dockerfile ]] || fail "this checkout has no voice gateway yet: update the code first (guide step 3)"
  grep -q 'voice/device-token' apps/server/src/modules/voice/routes.ts 2>/dev/null || fail "this checkout has no console voice pass route: update the code first"
  grep -q 'handle /voice-gateway' infrastructure/caddy/Caddyfile || fail "the Caddyfile has no /voice-gateway route"
  for key in LIVEKIT_URL LIVEKIT_API_KEY LIVEKIT_API_SECRET; do [[ -n "$(envval "$key")" ]] || fail "$key is empty: browser voice (LiveKit) must be set up first (docs/VOICE.md)"; done
  [[ -n "$(cid livekit)" ]] || fail "the LiveKit container is not running (docker compose --profile voice ps livekit)"
  [[ -n "$(cid db)" && -n "$(cid app)" ]] || fail "db or app is not running"
  docker image inspect "$ROLLBACK_TAG" >/dev/null 2>&1 || fail "rollback image $ROLLBACK_TAG missing (guide step 2: docker tag akeno-network:latest $ROLLBACK_TAG)"
  [[ -f "$ROLLBACK_ENV" ]] || fail "$ROLLBACK_ENV missing (guide step 2: cp .env $ROLLBACK_ENV)"
  [[ -s "$ROLLBACK_COMMIT_FILE" ]] || fail "$ROLLBACK_COMMIT_FILE missing (guide step 2: git rev-parse HEAD > $ROLLBACK_COMMIT_FILE before the code update)"
  local free_mb; free_mb="$(df -Pm /var/lib/docker 2>/dev/null | awk 'NR==2 {print $4}')"
  [[ -z "$free_mb" || "$free_mb" -ge 2048 ]] || fail "less than 2 GB free for Docker images (${free_mb} MB)"
  local avail_mb; avail_mb="$(free -m | awk '/^Mem:/ {print $7}')"
  [[ "$avail_mb" -ge 300 ]] || fail "less than 300 MB of memory available (${avail_mb} MB); the gateway needs ~60-100 MB"
  echo "  commit: $(git rev-parse --short HEAD) (rollback to $(cat "$ROLLBACK_COMMIT_FILE" 2>/dev/null | cut -c1-12))"
  echo "  LIVEKIT_NODE_IP=$(envval LIVEKIT_NODE_IP) LIVEKIT_USE_EXTERNAL_IP=$(envval LIVEKIT_USE_EXTERNAL_IP); interface IPv4: $(ip -4 -o addr show scope global 2>/dev/null | awk '{print $4}' | tr '\n' ' ')"
  echo "  VOICE_GATEWAY_URL now: '$(envval VOICE_GATEWAY_URL)' (will be wss://$domain/voice-gateway)"
  echo "  voice pass route on the running server: HTTP $(probe_route "$domain") (404 = not upgraded yet)"
  if ((blockers > 0)); then echo "Preflight found $blockers blocker(s)."; exit 1; fi
  echo "Preflight passed."
}

build() {
  preflight
  confirm "This builds the app and voice gateway images (several minutes of CPU on a small VPS; the website keeps running)."
  say "Build the app and voice gateway images (running containers are not touched)"
  "${COMPOSE[@]}" build app voice-gateway
  echo "Built. Next: $0 apply"
}

apply() {
  preflight
  local db_before lk_before image_now image_before
  image_now="$(docker image inspect --format '{{.Id}}' akeno-network:latest)"
  image_before="$(docker image inspect --format '{{.Id}}' "$ROLLBACK_TAG")"
  [[ "$image_now" != "$image_before" ]] || { echo "akeno-network:latest is still the previous image: run '$0 build' first." >&2; exit 1; }
  docker image inspect akeno-voice-gateway:latest >/dev/null 2>&1 || { echo "akeno-voice-gateway:latest missing: run '$0 build' first." >&2; exit 1; }
  db_before="$(cid db)"; lk_before="$(cid livekit)"
  confirm "This starts the voice gateway, sets VOICE_GATEWAY_URL and recreates the app (about 10 s without API) and Caddy (about 2 s without HTTPS)."
  say "1/4 Start the voice gateway"
  "${COMPOSE[@]}" up -d --no-deps --no-build voice-gateway
  wait_healthy voice-gateway 90
  say "2/4 VOICE_GATEWAY_URL=wss://$domain/voice-gateway"
  set_env VOICE_GATEWAY_URL "wss://$domain/voice-gateway"
  say "3/4 Recreate the app only (new image, new setting; no migration is pending)"
  "${COMPOSE[@]}" up -d --no-deps --no-build app
  wait_healthy app 180
  say "4/4 Recreate Caddy so it reads the new Caddyfile (a reload would keep the old bind-mounted file)"
  "${COMPOSE[@]}" up -d --no-deps --no-build --force-recreate caddy
  wait_healthy caddy 60
  [[ "$(cid db)" == "$db_before" ]] || echo "WARNING: the db container changed" >&2
  [[ "$(cid livekit)" == "$lk_before" ]] || echo "WARNING: the livekit container changed" >&2
  verify
}

verify() {
  say "Verify (read-only)"
  local ok=1 code
  code="$(http_status "https://$domain/health/ready")"; echo "  /health/ready: $code"; [[ "$code" == 200 ]] || ok=0
  code="$(probe_route "$domain")"; echo "  voice pass route with an invalid device token: $code (expected 401)"; [[ "$code" == 401 ]] || ok=0
  code="$(probe_ws "$domain")"; echo "  wss://$domain/voice-gateway upgrade: $code (expected 101)"; [[ "$code" == 101 ]] || ok=0
  code="$(http_status "https://$domain/livekit/")"; echo "  /livekit/ through Caddy: $code (LiveKit answers 200 or 404; 502 = not reachable)"; [[ "$code" != 502 && "$code" != 000 ]] || ok=0
  echo -n "  gateway /healthz: "; "${COMPOSE[@]}" exec -T voice-gateway node -e "fetch('http://127.0.0.1:7890/healthz').then(r=>r.text()).then(t=>console.log(t))" || { echo "no answer"; ok=0; }
  "${COMPOSE[@]}" ps --format 'table {{.Service}}\t{{.Status}}'
  echo "  server errors in the last 10 minutes:"
  "${COMPOSE[@]}" logs --no-color --since 10m app 2>/dev/null | grep -E '"level":(50|60)' | tail -10 | cut -c1-300 || echo "    none"
  if ((ok)); then echo "Verify passed. Next: browser voice with two browsers, then the PS5 (docs/server-voice/PS5-ERSTTEST-DE.md)."; else echo "Verify FAILED: see above; roll back if the website is affected." >&2; exit 1; fi
}

rollback() {
  local level="${1:-}"
  case "$level" in
    1)
      confirm "Rollback level 1: VOICE_GATEWAY_URL is emptied and the app recreated (consoles get 'Console voice is not set up'; browsers keep voice)."
      set_env VOICE_GATEWAY_URL ""
      "${COMPOSE[@]}" up -d --no-deps app
      wait_healthy app 180
      ;;
    2)
      confirm "Rollback level 2: the voice gateway is stopped (connected consoles leave voice; nothing else changes)."
      "${COMPOSE[@]}" stop voice-gateway
      ;;
    3)
      [[ -s "$ROLLBACK_COMMIT_FILE" && -f "$ROLLBACK_ENV" ]] || { echo "Rollback point missing ($ROLLBACK_COMMIT_FILE, $ROLLBACK_ENV)." >&2; exit 1; }
      docker image inspect "$ROLLBACK_TAG" >/dev/null || { echo "Rollback image $ROLLBACK_TAG missing." >&2; exit 1; }
      local previous; previous="$(cat "$ROLLBACK_COMMIT_FILE")"
      confirm "Rollback level 3: back to commit ${previous:0:12}, image $ROLLBACK_TAG and $ROLLBACK_ENV; the gateway is removed; app and Caddy are recreated. The database is not touched (no migration was applied)."
      "${COMPOSE[@]}" stop voice-gateway 2>/dev/null || true
      "${COMPOSE[@]}" rm -f voice-gateway 2>/dev/null || true
      # Detached at the previous commit: no branch is moved or rewritten; the upgrade commits stay
      # on GitHub. To upgrade again later: git checkout <branch> (it still points at the new code).
      git checkout --quiet --detach "$previous"
      cp -p "$ROLLBACK_ENV" .env
      docker tag "$ROLLBACK_TAG" akeno-network:latest
      docker compose --profile https up -d --no-deps --no-build app
      wait_healthy app 180
      docker compose --profile https up -d --no-deps --force-recreate caddy
      wait_healthy caddy 60
      echo "  commit now: $(git rev-parse --short HEAD); voice pass route: HTTP $(probe_route "$domain") (404 = old server again)"
      ;;
    *) echo "usage: $0 rollback 1|2|3" >&2; exit 2 ;;
  esac
  code="$(http_status "https://$domain/health/ready")"
  echo "  /health/ready: $code"
  [[ "$code" == 200 ]] || { echo "The website is not ready; check: docker compose ps; docker compose logs --tail 100 app" >&2; exit 1; }
}

case "${1:-}" in
  preflight) preflight ;;
  build) build ;;
  apply) apply ;;
  verify) verify ;;
  rollback) rollback "${2:-}" ;;
  *) sed -n '2,19p' "$0"; exit 2 ;;
esac
