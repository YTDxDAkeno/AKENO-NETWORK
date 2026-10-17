#!/usr/bin/env bash
# Safety gate for the PS5 build: fails if the ELF imports a system (sce*) function that has not
# been reviewed, if an optional import is not weak (the SDK runtime refuses to start a payload
# with an unresolved strong import), if AKENO code other than the platform layer loads libraries
# at run time, or if AKENO's own object files reference the SDK's kernel memory helpers.
#
#   check-imports.sh <build-ps5 directory> [nm]
# The audio lab build (build-ps5-audiolab, EXPERIMENTAL_AUDIO=1) is checked by the same rules:
# its audio functions are resolved at run time by platform_ps5.o and are never imported.
set -euo pipefail
build="${1:?usage: check-imports.sh <build-ps5 dir> [nm]}"
nm="${2:-}"
if [[ -z "$nm" && -n "${PS5_PAYLOAD_SDK:-}" ]]; then nm="$PS5_PAYLOAD_SDK/bin/prospero-nm"; fi
nm="${nm:-llvm-nm-18}"
elf="$(find "$build" -maxdepth 1 -name 'akeno-network-ps5*.elf' ! -name '*.debug.elf' | head -1)"
[[ -n "$elf" ]] || { echo "No akeno-network-ps5*.elf in $build" >&2; exit 1; }

# Reviewed system functions (see app/platform_ps5.c and docs/PS5_CLIENT.md).
allowed_sce='^(sceKernelSendNotificationRequest|sceSystemServiceLaunchWebBrowser|sceUserServiceInitialize|sceUserServiceTerminate|sceNetErrnoLoc|sceNetPoolCreate|sceNetPoolDestroy|sceNetResolverCreate|sceNetResolverDestroy|sceNetResolverStartAton|sceNetResolverStartNtoa)$'

# Reviewed, optional system functions: must be weak imports so that a firmware without them
# only disables the feature (game detection) instead of preventing the app from starting.
optional_sce='^(sceKernelGetAppInfo)$'

# Functions resolved at run time with dlopen/dlsym by app/platform_ps5.c only (never imported):
#   libSceNotification.sprx: sceNotificationSend (button notifications, opt-in)
#   libScePad.sprx: scePadInit/Open/GetHandle/ReadState/Close (experimental controller shortcut,
#   off by default, read-only), libSceUserService.sprx: sceUserServiceGetForegroundUser /
#   GetLoginUserIdList (which user's controller; the library itself is already linked)
#   audio lab build only: libSceAudioOut.sprx (sceAudioOutInit/Open/Output/Close, test tone) and
#   name lookups in libSceAudioOut.sprx / libSceAudioIn.sprx (nothing called)

status=0
imports="$("$nm" -D --undefined-only "$elf" | awk '{print $2}')"
weak_imports="$(readelf --dyn-syms -W "$elf" | awk '$5 == "WEAK" && $7 == "UND" {print $8}')"
while read -r symbol; do
  [[ -z "$symbol" ]] && continue
  if [[ "$symbol" =~ $optional_sce ]]; then
    if ! grep -qx "$symbol" <<< "$weak_imports"; then
      echo "Optional system import is not weak: $symbol" >&2
      status=1
    fi
  elif [[ "$symbol" == sce* ]] && ! [[ "$symbol" =~ $allowed_sce ]]; then
    echo "Unreviewed system import: $symbol" >&2
    status=1
  fi
done <<< "$imports"

# Every strong import must have resolved on a console before (reviewed-imports.txt).
reviewed="$(dirname "$0")/reviewed-imports.txt"
while read -r symbol; do
  [[ -z "$symbol" ]] && continue
  grep -qx "$symbol" <<< "$weak_imports" && continue
  if ! grep -qx "$symbol" "$reviewed"; then
    echo "Strong import not proven on a console (see reviewed-imports.txt): $symbol" >&2
    status=1
  fi
done <<< "$imports"

# Run-time library loading is confined to the platform layer.
for object in "$build"/obj/app/*.o "$build"/obj/client/*.o; do
  [[ "$(basename "$object")" == platform_ps5.o ]] && continue
  loads="$("$nm" --undefined-only "$object" | awk '{print $2}' | grep -E '^(dlopen|dlsym)$' || true)"
  if [[ -n "$loads" ]]; then
    echo "$(basename "$object") loads libraries at run time: $loads" >&2
    status=1
  fi
done

# AKENO code may only use the read-only firmware query from the SDK's kernel helpers.
for object in "$build"/obj/app/*.o "$build"/obj/client/*.o; do
  bad="$("$nm" --undefined-only "$object" | awk '{print $2}' | grep -E '^(kernel_|__crt_syscall|syscall$)' | grep -v '^kernel_get_fw_version$' || true)"
  if [[ -n "$bad" ]]; then
    echo "$(basename "$object") references kernel helpers: $bad" >&2
    status=1
  fi
done

# Microphone: no AKENO platform code may declare or resolve a capture function (libSceAudioIn
# names are only looked up by the audio lab, never called).
src="$(cd "$(dirname "$0")/.." && pwd)/app"
if grep -n 'sceAudioIn' "$src/platform_ps5.c" "$src/platform.h" "$src/platform_host.c" >&2; then
  echo "Platform code mentions libSceAudioIn functions (no capture code is allowed)" >&2
  status=1
fi

# The release ELF must not contain the audio lab at all: no audio function names, no lab page
# script, no report file name. (Bare library names such as libSceAudioOut.sprx appear in every
# payload: they are part of the SDK runtime's own list of system libraries.)
if [[ "$(basename "$elf")" != *audiolab* ]]; then
  if strings -a "$elf" | grep -E 'sceAudio(Out|In)[A-Za-z0-9]|AkenoAudioLab = function|audiolab-report|audio_lab' >&2; then
    echo "The release ELF contains audio lab code or strings" >&2
    status=1
  fi
fi

if [[ $status -eq 0 ]]; then
  echo "Imports OK: $(echo "$imports" | grep -c .) dynamic imports; system functions:"
  echo "$imports" | grep '^sce' | while read -r symbol; do
    if grep -qx "$symbol" <<< "$weak_imports"; then echo "  $symbol (weak, optional)"; else echo "  $symbol"; fi
  done
  echo "Loaded at run time by platform_ps5.o (opt-in): sceNotificationSend from libSceNotification.sprx;"
  echo "  experimental controller shortcut (off by default): scePad* from libScePad.sprx (read-only)"
  if [[ "$(basename "$elf")" == *audiolab* ]]; then
    echo "Audio lab build: libSceAudioOut.sprx / libSceAudioIn.sprx loaded at run time only when the user starts a check"
  fi
fi
exit $status
