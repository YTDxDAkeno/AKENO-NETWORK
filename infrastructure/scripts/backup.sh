#!/usr/bin/env bash
# Creates a backup of the AKENO NETWORK Docker Compose deployment:
#   ./backups/akeno-<UTC timestamp>.dump          PostgreSQL (custom format)
#   ./backups/akeno-media-<UTC timestamp>.tar.gz  uploaded images (media volume)
#
#   ./infrastructure/scripts/backup.sh
#   BACKUP_DIR=/srv/backups KEEP=30 ./infrastructure/scripts/backup.sh
#
# The database is dumped first and the media second, so the media archive contains every image
# the dump refers to (images uploaded in between are harmless extras that the server removes as
# orphans after a restore). Both files are needed to restore profiles completely; a database
# dump alone restores accounts and messages but not the pictures.
# Old backups beyond KEEP (default 14) are removed.
set -euo pipefail

BACKUP_DIR="${BACKUP_DIR:-./backups}"
KEEP="${KEEP:-14}"
COMPOSE=(docker compose)
if [[ -n "${COMPOSE_ENV_FILE:-}" ]]; then COMPOSE+=(--env-file "$COMPOSE_ENV_FILE"); fi

stamp="$(date -u +%Y%m%dT%H%M%SZ)"
name="akeno-${stamp}.dump"
media="akeno-media-${stamp}.tar.gz"
mkdir -p "$BACKUP_DIR"

"${COMPOSE[@]}" exec -T db pg_dump -U akeno -d akeno --format=custom --no-owner --file "/tmp/${name}"
"${COMPOSE[@]}" cp "db:/tmp/${name}" "${BACKUP_DIR}/${name}"
"${COMPOSE[@]}" exec -T db rm -f "/tmp/${name}"
echo "Database backup written to ${BACKUP_DIR}/${name} ($(du -h "${BACKUP_DIR}/${name}" | cut -f1))"

# Media: streamed out of the running app container (its root filesystem is read-only; tar only
# reads the volume). An empty volume still produces a valid, empty archive.
"${COMPOSE[@]}" exec -T app sh -c 'mkdir -p /data/media/objects && tar -C /data/media -cf - objects' | gzip -6 > "${BACKUP_DIR}/${media}.partial"
gzip -t "${BACKUP_DIR}/${media}.partial"
mv "${BACKUP_DIR}/${media}.partial" "${BACKUP_DIR}/${media}"
echo "Media backup written to ${BACKUP_DIR}/${media} ($(du -h "${BACKUP_DIR}/${media}" | cut -f1))"

# Retention: keep only the newest $KEEP backups of each kind.
for pattern in 'akeno-2*.dump' 'akeno-media-*.tar.gz'; do
  mapfile -t old < <(ls -1t "${BACKUP_DIR}"/${pattern} 2>/dev/null | tail -n +"$((KEEP + 1))")
  if ((${#old[@]} > 0)); then
    rm -f -- "${old[@]}"
    echo "Removed ${#old[@]} old backup(s) matching ${pattern}; keeping the newest ${KEEP}."
  fi
done
