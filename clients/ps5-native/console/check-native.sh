#!/usr/bin/env bash
# Safety gate for the AKENO NETWORK Native PS5 ELF (docs/PS5_NATIVE_ADR.md). Fails when:
#   - the ELF imports a system function outside the reviewed list below (AKENO's platform layer
#     plus SDL2's PS5 drivers), or a library outside the reviewed NEEDED list;
#   - an optional import is not weak (the SDK runtime refuses to start a payload with an
#     unresolved strong import);
#   - AKENO's own object files reference anything that changes the system or the pairing:
#     launching/killing apps, title registration, mounting, ptrace, the SDK's kernel memory helpers,
#     run-time library loading (except the platform layer's microphone, below), linked microphone
#     functions, pad focus/privilege, the browser, the realtime connection, or the credential-writing
#     parts of the client library;
#   - the platform layer loads anything at run time other than libSceAudioIn.sprx with exactly
#     sceAudioInOpen, sceAudioInInput and sceAudioInClose (party voice, docs/PS5_VOICE.md: only
#     after the user started voice or the microphone test);
#   - code other than the platform layer calls a console function.
#
#   check-native.sh <build-ps5 directory> [nm]
set -euo pipefail
build="${1:?usage: check-native.sh <build-ps5 dir> [nm]}"
nm="${2:-}"
if [[ -z "$nm" && -n "${PS5_PAYLOAD_SDK:-}" ]]; then nm="$PS5_PAYLOAD_SDK/bin/prospero-nm"; fi
nm="${nm:-llvm-nm-18}"
elf="$build/akeno-native.elf"
[[ -f "$elf" ]] || { echo "No akeno-native.elf in $build" >&2; exit 1; }

# Reviewed system imports. Source of each: AKENO = src/platform/platform_ps5.c (see its header),
# SDL = ps5-payload-dev/SDL ee4c47d (video, audio, joystick, keyboard, TV remote, IME, URL),
# SDK = ps5-payload-sdk f5d40f0 libc (DNS through the resolver, as in the daemon).
reviewed_sce='
sceAudioOutClose sceAudioOutInit sceAudioOutOpen sceAudioOutOutput
sceImeDialogGetResult sceImeDialogGetStatus sceImeDialogInit sceImeDialogTerm
sceKernelAllocateMainDirectMemory sceKernelCreateEqueue sceKernelDeleteEqueue sceKernelMapDirectMemory
sceKernelReleaseDirectMemory sceKernelWaitEqueue
sceKernelGetAppInfo sceKernelSendNotificationRequest
sceKeyboardClose sceKeyboardInit sceKeyboardOpen sceKeyboardReadState
sceNetErrnoLoc sceNetPoolCreate sceNetPoolDestroy sceNetResolverCreate sceNetResolverDestroy sceNetResolverStartAton
sceNetResolverStartNtoa
scePadClose scePadGetHandle scePadInit scePadOpen scePadReadState scePadSetLightBar scePadSetVibration scePadSetVibrationMode
sceSystemServiceGetAppIdOfRunningBigApp sceSystemServiceHideSplashScreen sceSystemServiceLaunchWebBrowser sceSystemServiceLoadExec
sceUserServiceGetForegroundUser sceUserServiceGetLoginUserIdList sceUserServiceGetUserName sceUserServiceInitialize
sceVideoOutAddFlipEvent sceVideoOutClose sceVideoOutDeleteFlipEvent sceVideoOutOpen sceVideoOutRegisterBuffers2
sceVideoOutSetBufferAttribute2 sceVideoOutSetFlipRate sceVideoOutSubmitFlip
'
# Must be weak: AKENO-only queries, so a firmware without them only loses the context check.
weak_required='sceKernelGetAppInfo sceSystemServiceGetAppIdOfRunningBigApp'
reviewed_needed='libSceSystemService.sprx libSceUserService.sprx libScePad.sprx libSceAudioOut.sprx libSceVideoOut.sprx
libSceKeyboard.sprx libSceImeDialog.sprx libkernel_web.sprx libSceLibcInternal.sprx libSceNet.sprx'

status=0
fail() {
  echo "check-native: $*" >&2
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

# AKENO's own objects: core, app, platform and the shared daemon helpers compiled into this ELF.
forbidden='^(dlopen|dlsym|ptrace|syscall|__crt_syscall|nmount|mount|unmount|kernel_.*|sceAudioIn.*|sceVoice.*|scePadSetProcess.*|scePadSetForceIntercepted|sceSystemServiceKillApp|sceSystemServiceLaunchApp|sceSystemServiceLaunchWebBrowser|sceLncUtil.*|sceAppInstUtil.*|sceRegMgr.*|sceKernelMprotect|SDL_OpenURL|akeno_pairing_start|akeno_pairing_wait|akeno_logout|akeno_forget_credentials|akeno_realtime_run)$'
for object in "$build"/obj/core/*.o "$build"/obj/app/*.o "$build"/obj/platform/*.o "$build"/obj/shared/*.o; do
  [[ -f "$object" ]] || continue
  refs="$("$nm" --undefined-only "$object" | awk '{print $2}')"
  bad="$(grep -E "$forbidden" <<< "$refs" | grep -vx 'kernel_get_fw_version' || true)"
  # the platform layer's microphone is loaded at run time (checked below)
  if [[ "$(basename "$object")" == platform_ps5.o ]]; then bad="$(grep -vxE 'dlopen|dlsym' <<< "$bad" || true)"; fi
  [[ -z "$bad" ]] || fail "$(basename "$object") references forbidden functions: $(echo $bad)"
  if [[ "$(basename "$object")" != platform_ps5.o ]]; then
    console="$(grep -E '^(sce|kernel_)' <<< "$refs" || true)"
    [[ -z "$console" ]] || fail "$(basename "$object") calls console functions outside the platform layer: $(echo $console)"
  fi
done

# The platform layer may only use what its header documents.
platform_allowed='kernel_get_fw_version sceKernelSendNotificationRequest sceKernelGetAppInfo sceSystemServiceGetAppIdOfRunningBigApp
sceSystemServiceHideSplashScreen sceSystemServiceLoadExec sceAudioOutInit sceAudioOutOpen sceAudioOutOutput sceAudioOutClose
sceUserServiceGetForegroundUser scePadGetHandle scePadReadState'
for symbol in $("$nm" --undefined-only "$build/obj/platform/platform_ps5.o" | awk '{print $2}' | grep -E '^(sce|kernel_)' || true); do
  grep -qw -- "$symbol" <<< "$platform_allowed" || fail "platform_ps5.o uses an undocumented function: $symbol"
done

# Run-time loading in the platform layer: one library, three functions (its strings are the names).
platform_strings="$(strings -a "$build/obj/platform/platform_ps5.o")"
sprx="$(grep -oE '[A-Za-z0-9_]+\.sprx' <<< "$platform_strings" | sort -u)"
[[ -z "$sprx" || "$sprx" == libSceAudioIn.sprx ]] || fail "platform_ps5.o names other libraries to load: $(echo $sprx)"
audio_in="$(grep -xE 'sceAudioIn[A-Za-z]*' <<< "$platform_strings" | sort -u | tr '\n' ' ')"
[[ "$audio_in" == "sceAudioInClose sceAudioInInput sceAudioInOpen " ]] || fail "platform_ps5.o resolves unexpected microphone functions: $audio_in"
for object in "$build"/obj/core/*.o "$build"/obj/app/*.o "$build"/obj/shared/*.o; do
  [[ -f "$object" ]] || continue
  if strings -a "$object" | grep -qE '\.sprx'; then fail "$(basename "$object") names a system library (only the platform layer may)"; fi
done

if [[ $status -eq 0 ]]; then
  echo "check-native: OK"
  echo "  system imports ($(grep -cE '^(sce|kernel_)' <<< "$imports")), all reviewed; weak: $(grep -E '^(sceKernelGetAppInfo|sceSystemServiceGetAppIdOfRunningBigApp)$' <<< "$weak" | tr '\n' ' ')"
  echo "  AKENO objects reference no launch, kill, registration, mount, ptrace, kernel, browser, realtime or"
  echo "  credential-writing function; only platform_ps5.o calls console functions. Loaded at run time by"
  echo "  platform_ps5.o, only when the user starts voice or the microphone test: libSceAudioIn.sprx"
  echo "  (sceAudioInOpen, sceAudioInInput, sceAudioInClose)."
fi
exit $status
