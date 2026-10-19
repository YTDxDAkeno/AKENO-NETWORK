#!/usr/bin/env bash
# Safety gate for the AKENO Party Voice ELF (docs/PS5_PARTY_VOICE.md). Fails when:
#   - it imports a system function outside the reviewed list (src/pv_backend_ps5.c's header names
#     where each one ran on 12.20 before), or links a library outside the reviewed NEEDED list;
#   - an optional query (the running app, the foreground user) is not a weak import;
#   - any object references anything that changes the system: launching/killing apps, mounting,
#     ptrace, raw syscalls, the SDK's kernel memory helpers, pad, registry or install utilities,
#     linked microphone functions, the voice/chat libraries;
#   - any object names an audio focus, attach/detach, app-module, volume, main-output or exclusive
#     HDMI function (none has a verified declaration; none may be looked up either);
#   - pv_backend_ps5.o names any library to load at run time other than libSceAudioIn.sprx, resolves
#     anything there but sceAudioInOpen/Input/Close, or code other than pv_backend_ps5.o calls a
#     console function or names a system library.
#
#   check-partyvoice.sh <build-ps5 directory> [nm]
set -euo pipefail
build="${1:?usage: check-partyvoice.sh <build-ps5 dir> [nm]}"
nm="${2:-}"
if [[ -z "$nm" && -n "${PS5_PAYLOAD_SDK:-}" ]]; then nm="$PS5_PAYLOAD_SDK/bin/prospero-nm"; fi
nm="${nm:-llvm-nm-18}"
elf="$build/akeno-party-voice.elf"
[[ -f "$elf" ]] || { echo "No akeno-party-voice.elf in $build" >&2; exit 1; }

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
  echo "check-partyvoice: $*" >&2
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

objects=("$build"/obj/pv/*.o "$build"/obj/native/*/*.o "$build"/obj/lab/*.o "$build"/obj/client/*.o)
forbidden='^(ptrace|syscall|__crt_syscall|nmount|mount|unmount|kernel_.*|sceVoice.*|sceVoiceChat.*|scePad.*|sceSystemServiceKillApp|sceSystemServiceLaunchApp|sceSystemServiceLaunchWebBrowser|sceSystemServiceLoadExec|sceLncUtil.*|sceAppInstUtil.*|sceRegMgr.*|sceKernelMprotect|sceAudioIn.*|sceAudioOut2.*|akeno_pairing_start|akeno_pairing_wait|akeno_logout|akeno_forget_credentials|akeno_realtime_run)$'
focus='(Focus|AttachToApplication|DetachFromApplication|ChangeAppModuleState|SetVolume|SetMainOutput|ExConfigureOutput|EnableChat|DisableChat)'
for object in "${objects[@]}"; do
  [[ -f "$object" ]] || continue
  name="$(basename "$object")"
  refs="$("$nm" --undefined-only "$object" | awk '{print $2}')"
  bad="$(grep -E "$forbidden" <<< "$refs" | grep -vx 'kernel_get_fw_version' || true)"
  [[ -z "$bad" ]] || fail "$name references forbidden functions: $(echo $bad)"
  if strings -a "$object" | grep -E "sceAudioOut[A-Za-z0-9]*$focus" > /dev/null; then fail "$name names an audio focus/routing/volume function"; fi
  if [[ "$name" != pv_backend_ps5.o ]]; then
    console="$(grep -E '^(sce|kernel_|dlopen|dlsym)' <<< "$refs" || true)"
    [[ -z "$console" ]] || fail "$name calls console functions outside pv_backend_ps5.o: $(echo $console)"
    if strings -a "$object" | grep -qE '\.sprx'; then fail "$name names a system library (only pv_backend_ps5.o may)"; fi
  fi
done

# Run-time loading: only pv_backend_ps5.o, one library, three functions.
backend_strings="$(strings -a "$build/obj/pv/pv_backend_ps5.o")"
sprx="$(grep -oE '[A-Za-z0-9_]+\.sprx' <<< "$backend_strings" | sort -u)"
[[ "$sprx" == libSceAudioIn.sprx ]] || fail "pv_backend_ps5.o names other libraries to load: $(echo $sprx)"
audio_in="$(grep -xE 'sceAudioIn[A-Za-z]*' <<< "$backend_strings" | sort -u | tr '\n' ' ')"
[[ "$audio_in" == "sceAudioInClose sceAudioInInput sceAudioInOpen " ]] || fail "pv_backend_ps5.o resolves unexpected microphone functions: $audio_in"
# The only file it may write is under /data/akeno-party-voice; /data/akeno is read (key, settings, CA).
if strings -a "$build/obj/pv/pv_main.o" | grep -xE '/data/[a-z0-9_-]+' | grep -vxE '/data/akeno|/data/akeno-party-voice' > /dev/null; then
  fail "pv_main.o names another data directory"
fi

if (( status == 0 )); then
  echo "check-partyvoice: OK"
  echo "  $(grep -cE '^(sce|kernel_)' <<< "$imports" || true) reviewed system imports; optional ones weak; libraries: $(readelf -d "$elf" | awk '/NEEDED/ {gsub(/[\[\]]/, "", $5); print $5}' | sort | tr '\n' ' ')"
  echo "  no launch, kill, mount, ptrace, kernel, pad, registry, voice-chat, audio focus/attach/volume/routing function;"
  echo "  only pv_backend_ps5.o calls console functions; loaded at run time, only after the user turns the microphone"
  echo "  on: libSceAudioIn.sprx (sceAudioInOpen, sceAudioInInput, sceAudioInClose)."
fi
exit "$status"
