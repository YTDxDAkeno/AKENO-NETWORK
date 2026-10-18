#!/usr/bin/env bash
# Safety gate for the AKENO Voice Lab ELF (docs/PS5_BACKGROUND_VOICE.md). Fails when:
#   - it imports a system function outside the reviewed list (src/backend_ps5.c's header names
#     where each was used before), or links a library outside the reviewed NEEDED list;
#   - an optional query (the running app, the foreground user) is not a weak import;
#   - any object references anything that changes the system: launching/killing apps, mounting,
#     ptrace, raw syscalls, the SDK's kernel memory helpers, pad focus, registry or install utilities;
#   - backend_ps5.o names any library to load at run time other than libSceAudioIn.sprx, resolves
#     anything there but sceAudioInOpen/Input/Close, or code other than backend_ps5.o calls a console
#     function or names a system library.
#
#   check-voicelab.sh <build-ps5 directory> [nm]
set -euo pipefail
build="${1:?usage: check-voicelab.sh <build-ps5 dir> [nm]}"
nm="${2:-}"
if [[ -z "$nm" && -n "${PS5_PAYLOAD_SDK:-}" ]]; then nm="$PS5_PAYLOAD_SDK/bin/prospero-nm"; fi
nm="${nm:-llvm-nm-18}"
elf="$build/akeno-voicelab.elf"
[[ -f "$elf" ]] || { echo "No akeno-voicelab.elf in $build" >&2; exit 1; }

reviewed_sce='
sceAudioOutClose sceAudioOutInit sceAudioOutOpen sceAudioOutOutput
sceKernelSendNotificationRequest
sceNetErrnoLoc sceNetPoolCreate sceNetPoolDestroy sceNetResolverCreate sceNetResolverDestroy sceNetResolverStartAton
sceNetResolverStartNtoa
sceSystemServiceGetAppIdOfRunningBigApp
sceUserServiceGetForegroundUser sceUserServiceInitialize
kernel_get_fw_version
'
weak_required='sceSystemServiceGetAppIdOfRunningBigApp sceUserServiceGetForegroundUser'
reviewed_needed='libSceSystemService.sprx libSceUserService.sprx libSceAudioOut.sprx libkernel_web.sprx
libSceLibcInternal.sprx libSceNet.sprx libkernel.sprx libkernel_sys.sprx'

status=0
fail() {
  echo "check-voicelab: $*" >&2
  status=1
}

imports="$("$nm" -D --undefined-only "$elf" | awk '{print $2}' | sort -u)"
weak="$(readelf --dyn-syms -W "$elf" | awk '$5 == "WEAK" && $7 == "UND" {print $8}')"
for symbol in $(grep -E '^(sce|kernel_)' <<< "$imports" || true); do
  grep -qw -- "$symbol" <<< "$reviewed_sce" || fail "unreviewed system import: $symbol"
done
for symbol in $weak_required; do
  if grep -qx "$symbol" <<< "$imports"; then
    grep -qx "$symbol" <<< "$weak" || fail "optional import is not weak: $symbol"
  fi
done
for lib in $(readelf -d "$elf" | awk '/NEEDED/ {gsub(/[\[\]]/, "", $5); print $5}'); do
  grep -qw -- "$lib" <<< "$reviewed_needed" || fail "unreviewed library: $lib"
done

forbidden='^(ptrace|syscall|__crt_syscall|nmount|mount|unmount|kernel_.*|sceVoice.*|scePad.*|sceSystemServiceKillApp|sceSystemServiceLaunchApp|sceSystemServiceLaunchWebBrowser|sceSystemServiceLoadExec|sceLncUtil.*|sceAppInstUtil.*|sceRegMgr.*|sceKernelMprotect|sceAudioIn.*)$'
for object in "$build"/obj/lab/*.o "$build"/obj/client/*.o; do
  [[ -f "$object" ]] || continue
  name="$(basename "$object")"
  refs="$("$nm" --undefined-only "$object" | awk '{print $2}')"
  bad="$(grep -E "$forbidden" <<< "$refs" | grep -vx 'kernel_get_fw_version' || true)"
  [[ -z "$bad" ]] || fail "$name references forbidden functions: $(echo $bad)"
  if [[ "$name" != backend_ps5.o ]]; then
    console="$(grep -E '^(sce|kernel_|dlopen|dlsym)' <<< "$refs" || true)"
    [[ -z "$console" ]] || fail "$name calls console functions outside backend_ps5.o: $(echo $console)"
  fi
done

# Run-time loading: only backend_ps5.o, one library, three functions (its strings are the names; the
# whole ELF also carries the SDK runtime's table of system library names, so objects are checked).
backend_strings="$(strings -a "$build/obj/lab/backend_ps5.o")"
sprx="$(grep -oE '[A-Za-z0-9_]+\.sprx' <<< "$backend_strings" | sort -u)"
[[ "$sprx" == libSceAudioIn.sprx ]] || fail "backend_ps5.o names other libraries to load: $(echo $sprx)"
audio_in="$(grep -xE 'sceAudioIn[A-Za-z]*' <<< "$backend_strings" | sort -u | tr '\n' ' ')"
[[ "$audio_in" == "sceAudioInClose sceAudioInInput sceAudioInOpen " ]] || fail "backend_ps5.o resolves unexpected microphone functions: $audio_in"
for object in "$build"/obj/lab/*.o "$build"/obj/client/*.o; do
  [[ "$(basename "$object")" == backend_ps5.o ]] && continue
  if strings -a "$object" | grep -qE '\.sprx'; then fail "$(basename "$object") names a system library (only backend_ps5.o may)"; fi
done

if (( status == 0 )); then
  echo "check-voicelab: $(grep -cE '^(sce|kernel_)' <<< "$imports" || true) reviewed system imports; no forbidden references"
fi
exit "$status"
