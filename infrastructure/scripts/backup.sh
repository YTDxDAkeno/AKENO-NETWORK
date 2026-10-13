#!/usr/bin/env bash
# Creates a compressed PostgreSQL backup of the AKENO NETWORK Docker Compose deployment.
#
#   ./infrastructure/scripts/backup.sh            # writes ./backups/akeno-<UTC timestamp>.dump
#   BACKUP_DIR=/srv/backups KEEP=30 ./infrastructure/scripts/backup.sh
#
# The dump is created inside the database container and copied out with `docker compose cp`,
# which is binary-safe on every platform. Old backups beyond KEEP (default 14) are removed.
set -euo pipefail

BACKUP_DIR="${BACKUP_DIR:-./backups}"
KEEP="${KEEP:-14}"
COMPOSE=(docker compose)
if [[ -n "${COMPOSE_ENV_FILE:-}" ]]; then COMPOSE+=(--env-file "$COMPOSE_ENV_FILE"); fi

stamp="$(date -u +%Y%m%dT%H%M%SZ)"
name="akeno-${stamp}.dump"
mkdir -p "$BACKUP_DIR"

"${COMPOSE[@]}" exec -T db pg_dump -U akeno -d akeno --format=custom --no-owner --file "/tmp/${name}"
"${COMPOSE[@]}" cp "db:/tmp/${name}" "${BACKUP_DIR}/${name}"
"${COMPOSE[@]}" exec -T db rm -f "/tmp/${name}"

echo "Backup written to ${BACKUP_DIR}/${name} ($(du -h "${BACKUP_DIR}/${name}" | cut -f1))"

# Retention: keep only the newest $KEEP backups.
mapfile -t old < <(ls -1t "${BACKUP_DIR}"/akeno-*.dump 2>/dev/null | tail -n +"$((KEEP + 1))")
if ((${#old[@]} > 0)); then
  rm -f -- "${old[@]}"
  echo "Removed ${#old[@]} old backup(s); keeping the newest ${KEEP}."
fi
