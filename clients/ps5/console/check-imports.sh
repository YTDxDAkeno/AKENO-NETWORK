#!/usr/bin/env bash
# Safety gate for the PS5 build: fails if the ELF imports a system (sce*) function that has not
# been reviewed, or if AKENO's own object files reference the SDK's kernel memory helpers.
#
#   check-imports.sh <build-ps5 directory> [nm]
set -euo pipefail
build="${1:?usage: check-imports.sh <build-ps5 dir> [nm]}"
nm="${2:-llvm-nm-18}"
elf="$build/akeno-network-ps5.elf"

# Reviewed system functions (see app/platform_ps5.c and docs/PS5_CLIENT.md).
allowed_sce='^(sceKernelSendNotificationRequest|sceSystemServiceLaunchWebBrowser|sceUserServiceInitialize|sceUserServiceTerminate|sceNetErrnoLoc|sceNetPoolCreate|sceNetPoolDestroy|sceNetResolverCreate|sceNetResolverDestroy|sceNetResolverStartAton|sceNetResolverStartNtoa)$'

status=0
imports="$("$nm" -D --undefined-only "$elf" | awk '{print $2}')"
while read -r symbol; do
  [[ -z "$symbol" ]] && continue
  if [[ "$symbol" == sce* ]] && ! [[ "$symbol" =~ $allowed_sce ]]; then
    echo "Unreviewed system import: $symbol" >&2
    status=1
  fi
done <<< "$imports"

# AKENO code may only use the read-only firmware query from the SDK's kernel helpers.
for object in "$build"/obj/app/*.o "$build"/obj/client/*.o; do
  bad="$("$nm" --undefined-only "$object" | awk '{print $2}' | grep -E '^(kernel_|__crt_syscall|syscall$)' | grep -v '^kernel_get_fw_version$' || true)"
  if [[ -n "$bad" ]]; then
    echo "$(basename "$object") references kernel helpers: $bad" >&2
    status=1
  fi
done

if [[ $status -eq 0 ]]; then
  echo "Imports OK: $(echo "$imports" | grep -c .) dynamic imports; system functions:"
  echo "$imports" | grep '^sce' | sed 's/^/  /'
fi
exit $status
