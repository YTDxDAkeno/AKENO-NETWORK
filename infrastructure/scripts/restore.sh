#!/usr/bin/env bash
# Restores an AKENO NETWORK backup created by backup.sh into the Docker Compose deployment.
#
#   ./infrastructure/scripts/restore.sh backups/akeno-20261013T190000Z.dump [backups/akeno-media-20261013T190000Z.tar.gz] [--yes]
#
# This REPLACES the current database contents (and, when a media archive is given, all uploaded
# images). The app container is stopped during the restore so no writes happen; the database
# restore runs in a single transaction (all or nothing). Without a media archive the images on
# the volume are left as they are; pictures whose files are missing fall back to preset avatars.
set -euo pipefail

file="${1:-}"
media=""
yes=""
for arg in "${@:2}"; do
  case "$arg" in
    --yes) yes=1 ;;
    *) media="$arg" ;;
  esac
done
if [[ -z "$file" || ! -f "$file" ]]; then
  echo "Usage: $0 <backup.dump> [media.tar.gz] [--yes]" >&2
  exit 1
fi
if [[ -n "$media" ]]; then
  [[ -f "$media" ]] || { echo "Media archive not found: $media" >&2; exit 1; }
  gzip -t "$media"
fi
COMPOSE=(docker compose)
if [[ -n "${COMPOSE_ENV_FILE:-}" ]]; then COMPOSE+=(--env-file "$COMPOSE_ENV_FILE"); fi

if [[ -z "$yes" ]]; then
  read -r -p "This replaces ALL current AKENO NETWORK data with ${file}${media:+ and ${media}}. Type RESTORE to continue: " answer
  [[ "$answer" == "RESTORE" ]] || { echo "Cancelled."; exit 1; }
fi

"${COMPOSE[@]}" stop app
"${COMPOSE[@]}" cp "$file" db:/tmp/restore.dump
"${COMPOSE[@]}" exec -T db pg_restore -U akeno -d akeno --clean --if-exists --no-owner --single-transaction /tmp/restore.dump
"${COMPOSE[@]}" exec -T db rm -f /tmp/restore.dump

if [[ -n "$media" ]]; then
  # A one-off container with the app image and the media volume (the app itself is stopped).
  # The archive is unpacked next to the old objects first and swapped in with two renames.
  gzip -dc "$media" | "${COMPOSE[@]}" run --rm --no-deps -T --entrypoint sh app -c \
    'set -e; cd /data/media; rm -rf restore-new objects-old; mkdir restore-new; tar -C restore-new -xf -; \
     [ -d restore-new/objects ] || mkdir restore-new/objects; \
     if [ -d objects ]; then mv objects objects-old; fi; mv restore-new/objects objects; rm -rf restore-new objects-old'
  echo "Media restored from ${media}."
fi

"${COMPOSE[@]}" start app
echo "Restore complete. The app is starting again; check: docker compose ps"
