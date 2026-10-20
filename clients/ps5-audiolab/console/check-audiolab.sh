#!/usr/bin/env bash
# Safety gate for the AKENO Audio Lab 3.1 ELF. Fails when:
#   - it imports a system function outside the reviewed read-only set;
#   - it links a library outside the reviewed NEEDED set;
#   - any object references a system-changing or audio-focus/routing/volume/microphone function
#     (the audio-output functions are resolved with dlopen at run time, so they must NOT be ELF
#     imports; port-state inspection is excluded entirely);
#   - any object other than al_backend_ps5.o calls a console function or dlopen/dlsym;
#   - al_backend_ps5.o names a library to dlopen other than libSceAudioOut.sprx.
#
#   check-audiolab.sh <build-ps5 directory> [nm]
set -euo pipefail
build="${1:?usage: check-audiolab.sh <build-ps5 dir> [nm]}"
nm="${2:-}"
if [[ -z "$nm" && -n "${PS5_PAYLOAD_SDK:-}" ]]; then nm="$PS5_PAYLOAD_SDK/bin/prospero-nm"; fi
nm="${nm:-llvm-nm-18}"
elf="$build/akeno-audio-lab.elf"
[[ -f "$elf" ]] || { echo "No akeno-audio-lab.elf in $build" >&2; exit 1; }

# Every linked/imported system symbol must be in this read-only set. The audio functions are NOT
# here on purpose: they are resolved with dlopen/dlsym and must never be static imports.
reviewed_sce='
sceKernelSendNotificationRequest
kernel_get_fw_version
'
reviewed_needed='libkernel_web.sprx libSceLibcInternal.sprx libkernel.sprx libkernel_sys.sprx libSceNet.sprx'

status=0
fail() { echo "check-audiolab: $*" >&2; status=1; }

imports="$("$nm" -D --undefined-only "$elf" | awk '{print $2}' | sort -u)"
for symbol in $(grep -E '^(sce|kernel_)' <<< "$imports" || true); do
  grep -qw -- "$symbol" <<< "$reviewed_sce" || fail "unreviewed system import: $symbol"
done
for lib in $(readelf -d "$elf" | awk '/NEEDED/ {gsub(/[\[\]]/, "", $5); print $5}'); do
  grep -qw -- "$lib" <<< "$reviewed_needed" || fail "unreviewed library: $lib"
done

# No audio symbol may be a static import: audio is dlopen-only.
if "$nm" -D --undefined-only "$elf" | grep -qE 'sceAudio(Out|In)'; then
  fail "the ELF statically imports an audio symbol (audio must be dlopen-only)"
fi

# No object may reference a system-changing, focus/routing/volume or microphone function anywhere.
forbidden='^(ptrace|syscall|__crt_syscall|nmount|mount|unmount|kernel_(copyin|copyout|set.*|get_proc.*|mprotect|dynlib.*|overlap.*|.*ucred.*|.*qaflags.*)|sceVoice.*|sceProprietary.*|scePad.*|sceAudioIn.*|sceAudioOutSetVolume|sceAudioOutSetMainOutput|sceAudioOutGetFocusEnablePid|sceAudioOutAttachToApplicationByPid|sceAudioOutDetachFromApplicationByPid|sceAudioOutChangeAppModuleState|sceVideoRecording.*|sceGameLiveStreaming.*|sceScreenShot.*|sceSystemService.*|sceUserService.*|sceLncUtil.*|sceAppInstUtil.*|sceRegMgr.*|sceKernelMprotect)$'
for object in "$build"/obj/*.o; do
  [[ -f "$object" ]] || continue
  name="$(basename "$object")"
  refs="$("$nm" --undefined-only "$object" | awk '{print $2}')"
  bad="$(grep -E "$forbidden" <<< "$refs" | grep -vx 'kernel_get_fw_version' || true)"
  [[ -z "$bad" ]] || fail "$name references forbidden functions: $(echo $bad)"
  if [[ "$name" != al_backend_ps5.o ]]; then
    console="$(grep -E '^(sce|kernel_|dlopen|dlsym|dlclose)' <<< "$refs" || true)"
    [[ -z "$console" ]] || fail "$name calls console functions outside al_backend_ps5.o: $(echo $console)"
  fi
done

# al_backend_ps5.o may name only libSceAudioOut.sprx for dlopen; every .sprx string in any object
# must be a known library.
known_libs="libSceAudioOut.sprx $reviewed_needed"
for object in "$build"/obj/*.o; do
  [[ -f "$object" ]] || continue
  for sprx in $(strings -a "$object" | grep -oE '[A-Za-z0-9_]+\.sprx' | sort -u); do
    grep -qw -- "$sprx" <<< "$known_libs" || fail "$(basename "$object") names an unknown library: $sprx"
  done
done
# libSceAudioIn must never appear anywhere (no microphone in this experiment).
if strings -a "$build"/obj/*.o | grep -qiE 'libSceAudioIn\.sprx'; then
  fail "libSceAudioIn.sprx is referenced; this experiment never opens the microphone"
fi
# Port state is intentionally disabled. The symbol must not even be resolved dynamically.
if strings -a "$build"/obj/*.o | grep -q 'sceAudioOutGetPortState'; then
  fail "sceAudioOutGetPortState is referenced; port-state inspection must remain disabled"
fi

if (( status == 0 )); then
  echo "check-audiolab: $(grep -cE '^(sce|kernel_)' <<< "$imports" || true) reviewed system imports; audio is dlopen-only; no forbidden references"
fi
exit "$status"
