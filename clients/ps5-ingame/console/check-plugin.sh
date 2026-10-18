#!/usr/bin/env bash
# Safety gate for the EXPERIMENTAL AKENO In-Game Companion plugin ELF: it may import only the C
# library, sockets, threads and clocks, and need only libkernel, libc and libSceNet. Any console
# system function (sce*), process, memory-protection, module-loading or kernel call fails the gate:
# the companion draws nothing itself and touches no other process; OnionHEN shows its page.
#
#   clients/ps5-ingame/console/check-plugin.sh clients/ps5-ingame/build-ps5
set -euo pipefail
build="${1:?build directory}"
elf="$build/AKEN00001.elf"
nm="${PS5_PAYLOAD_SDK:?}/bin/prospero-nm"
test -f "$elf"
allowed='^(__error|__isfinite|__stderrp|__stdoutp|abort|calloc|clock_gettime|close|connect|fcntl|fflush|fprintf|free|fstat|getsockopt|isalnum|isxdigit|lstat|malloc|memchr|memcmp|memcpy|memmove|memset|open|poll|printf|pthread_mutex_destroy|pthread_mutex_init|pthread_mutex_lock|pthread_mutex_unlock|qsort|read|realloc|recv|send|setsockopt|signal|snprintf|socket|sscanf|strcmp|strcspn|strlen|strncasecmp|strncmp|strpbrk|strrchr|strstr|strtod|strtoll|strtoul|time|tolower|toupper|usleep|vsnprintf|fputs|puts|putchar|fwrite|strchr|strtol|memrchr|nanosleep)$'
bad=$("$nm" -D --undefined-only "$elf" | awk '{print $2}' | grep -Ev "$allowed" || true)
if [[ -n "$bad" ]]; then
  echo "check-plugin: imports outside the reviewed set:" >&2
  echo "$bad" | sed 's/^/  /' >&2
  exit 1
fi
libs=$(readelf -d "$elf" | awk '/NEEDED/ {gsub(/[\[\]]/, "", $5); print $5}' | sort | tr '\n' ' ')
if [[ "$libs" != "libSceLibcInternal.sprx libSceNet.sprx libkernel_web.sprx " ]]; then
  echo "check-plugin: unexpected libraries: $libs" >&2
  exit 1
fi
count=$("$nm" -D --undefined-only "$elf" | wc -l)
echo "check-plugin: OK"
echo "  $count imports, all C library, sockets, threads or clocks; no console system function (sce*);"
echo "  libraries: $libs"
