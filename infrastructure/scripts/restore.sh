#!/usr/bin/env bash
# Restores an AKENO NETWORK backup created by backup.sh into the Docker Compose database.
#
#   ./infrastructure/scripts/restore.sh backups/akeno-20261013T190000Z.dump
#
# This REPLACES the current database contents. The app container is stopped during the restore
# so no writes happen, and the restore runs in a single transaction (all or nothing).
set -euo pipefail

file="${1:-}"
if [[ -z "$file" || ! -f "$file" ]]; then
  echo "Usage: $0 <backup.dump> [--yes]" >&2
  exit 1
fi
COMPOSE=(docker compose)
if [[ -n "${COMPOSE_ENV_FILE:-}" ]]; then COMPOSE+=(--env-file "$COMPOSE_ENV_FILE"); fi

if [[ "${2:-}" != "--yes" ]]; then
  read -r -p "This replaces ALL current AKENO NETWORK data with ${file}. Type RESTORE to continue: " answer
  [[ "$answer" == "RESTORE" ]] || { echo "Cancelled."; exit 1; }
fi

"${COMPOSE[@]}" stop app
"${COMPOSE[@]}" cp "$file" db:/tmp/restore.dump
"${COMPOSE[@]}" exec -T db pg_restore -U akeno -d akeno --clean --if-exists --no-owner --single-transaction /tmp/restore.dump
"${COMPOSE[@]}" exec -T db rm -f /tmp/restore.dump
"${COMPOSE[@]}" start app
echo "Restore complete. The app is starting again; check: docker compose ps"
