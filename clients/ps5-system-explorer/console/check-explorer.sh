#!/usr/bin/env bash
# Safety gate for the AKENO System Explorer ELF (docs/PHASE6_AKENO_SYSTEM_EXPLORER_DE.md). Fails when:
#   - it imports a system function outside the reviewed read-only list (ex_backend_ps5.c's header
#     names where each ran on 12.20 before), or links a library outside the reviewed NEEDED list;
#   - an optional query (the running app, the foreground user, this process's app info) is not a weak import;
#   - any object references anything that changes the system or another process: launching/killing
#     apps, mounting, ptrace, raw syscalls, the SDK's kernel memory helpers, pad, registry, install
#     utilities, or ANY audio/microphone/voice/video-recording/broadcast function (the default build
#     opens no audio port and no microphone);
#   - any object other than ex_backend_ps5.o calls a console function or names a system library;
#   - ex_backend_ps5.o names a library to dlopen that is not in the reviewed look-up set, or one the
#     catalogue marks quarantined.
#
#   check-explorer.sh <build-ps5 directory> [nm]
set -euo pipefail
build="${1:?usage: check-explorer.sh <build-ps5 dir> [nm]}"
nm="${2:-}"
if [[ -z "$nm" && -n "${PS5_PAYLOAD_SDK:-}" ]]; then nm="$PS5_PAYLOAD_SDK/bin/prospero-nm"; fi
nm="${nm:-llvm-nm-18}"
elf="$build/akeno-system-explorer.elf"
[[ -f "$elf" ]] || { echo "No akeno-system-explorer.elf in $build" >&2; exit 1; }

# Every linked/imported system symbol must be in this read-only set.
reviewed_sce='
sceKernelSendNotificationRequest
sceKernelGetAppInfo
sceSystemServiceGetAppIdOfRunningBigApp
sceSystemServiceGetAppTitleId
sceUserServiceGetForegroundUser sceUserServiceInitialize
kernel_get_fw_version
'
weak_required='sceKernelGetAppInfo sceSystemServiceGetAppIdOfRunningBigApp sceSystemServiceGetAppTitleId sceUserServiceGetForegroundUser'
reviewed_needed='libSceSystemService.sprx libSceUserService.sprx libkernel_web.sprx
libSceLibcInternal.sprx libSceNet.sprx libkernel.sprx libkernel_sys.sprx'

# Libraries ex_backend_ps5.o may dlopen for look-up only (never the quarantined ones).
reviewed_dlopen='libSceAudioOut.sprx libSceAudioIn.sprx libSceUserService.sprx libSceSystemService.sprx
libScePad.sprx libSceVideoOut.sprx libSceRemoteplay.sprx libSceScreenShot.sprx'
forbidden_dlopen='libSceVideoRecording.sprx libSceGameLiveStreaming.sprx'

status=0
fail() {
  echo "check-explorer: $*" >&2
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

# No object may reference a system-changing or audio/microphone function (anywhere in the build).
forbidden='^(ptrace|syscall|__crt_syscall|nmount|mount|unmount|kernel_(copyin|copyout|set.*|get_proc.*|mprotect|dynlib.*|overlap.*|.*ucred.*|.*qaflags.*)|sceVoice.*|sceProprietary.*|scePad.*|sceAudioOut.*|sceAudioIn.*|sceVideoRecording.*|sceGameLiveStreaming.*|sceScreenShot.*|sceSystemServiceKillApp|sceSystemServiceLaunchApp|sceSystemServiceLaunchWebBrowser|sceSystemServiceLoadExec|sceLncUtil.*|sceAppInstUtil.*|sceRegMgr.*|sceKernelMprotect)$'
for object in "$build"/obj/*.o; do
  [[ -f "$object" ]] || continue
  name="$(basename "$object")"
  refs="$("$nm" --undefined-only "$object" | awk '{print $2}')"
  bad="$(grep -E "$forbidden" <<< "$refs" | grep -vx 'kernel_get_fw_version' || true)"
  [[ -z "$bad" ]] || fail "$name references forbidden functions: $(echo $bad)"
  if [[ "$name" != ex_backend_ps5.o ]]; then
    console="$(grep -E '^(sce|kernel_|dlopen|dlsym|dlclose)' <<< "$refs" || true)"
    [[ -z "$console" ]] || fail "$name calls console functions outside ex_backend_ps5.o: $(echo $console)"
  fi
done

# Run-time loading. dlopen/dlsym/dlclose are referenced only by ex_backend_ps5.o (enforced by the
# console-function check above, which includes them). Two further binary-checkable properties:
#   1. ex_backend_ps5.o hardcodes NO library name: it can only dlopen a name it is handed, so the
#      set of loadable libraries is exactly the catalogue in ex_libraries.o (reviewed by code review
#      and the unit tests, which assert quarantined entries are never passed to lib_lookup).
#   2. every .sprx string anywhere in the build is a known library (loadable, quarantined-as-data,
#      or a reviewed NEEDED runtime library): no unexpected library name slips in.
known_libs="$reviewed_dlopen $forbidden_dlopen $reviewed_needed"
if strings -a "$build/obj/ex_backend_ps5.o" | grep -qoE '[A-Za-z0-9_]+\.sprx'; then
  fail "ex_backend_ps5.o hardcodes a library name (it must dlopen only names passed to it)"
fi
for object in "$build"/obj/*.o; do
  [[ -f "$object" ]] || continue
  for sprx in $(strings -a "$object" | grep -oE '[A-Za-z0-9_]+\.sprx' | sort -u); do
    grep -qw -- "$sprx" <<< "$known_libs" || fail "$(basename "$object") names an unknown library: $sprx"
  done
done

# The default build must not contain the optional audio probe.
if "$nm" -D --undefined-only "$elf" | grep -qE 'sceAudio(Out|In)'; then
  fail "the ELF imports an audio symbol: the active audio probe must not be in the default build"
fi

if (( status == 0 )); then
  echo "check-explorer: $(grep -cE '^(sce|kernel_)' <<< "$imports" || true) reviewed system imports; no forbidden references; no audio/microphone symbols"
fi
exit "$status"
