# PS5 voice: audio research and the experimental audio lab

Status 2026-10-14. **Nothing in this document has run on a PS5.** PS5 voice is experimental
and capability-dependent; desktop and phone voice (see [VOICE.md](VOICE.md)) do not depend on it.

## Report

| Question | Answer |
| --- | --- |
| **What compiles** | The release console app and a separate **audio lab** ELF (`make -C clients/ps5/console EXPERIMENTAL_AUDIO=1`), both with the pinned ps5-payload-sdk `f5d40f0`, `-Werror`. Both pass the import safety gate; the audio lab imports exactly the same 96 dynamic symbols as the release ELF. |
| **What known SDK interfaces support** | Audio **output** only: the SDK has a `libSceAudioOut` stub (136 symbol names, no declarations), and the PS5 audio driver of [ps5-payload-dev/SDL](https://github.com/ps5-payload-dev/SDL) (commit `ee4c47d`) declares and uses `sceAudioOutInit/Open/Output/Close`. **Microphone capture has no SDK interface**: there is no `libSceAudioIn` stub and no PS5 homebrew implementation was found. |
| **What is tested on Linux** | The audio lab's logic with a simulated platform: probe report, explicit confirmation, one tone at a time, failure isolation, the diagnostics-page section in Chromium, and that the release build has none of it (`clients/ps5/tests/integration/run.mjs --audiolab`, 5 scenarios, passing). |
| **What is verified on PS5 hardware** | **Nothing.** The audio lab has never run on a console. |
| **What remains unsupported** | Microphone capture (prototype B), device/headset enumeration, voice from a PS5 into an AKENO party (prototype D), audio while a game runs (prototype E), rest mode behaviour, firmware 13.60. |

## Sources inspected

| Source | Finding |
| --- | --- |
| ps5-payload-sdk `f5d40f02770e9b0001dd9e18056eca70b3da8b40` (`sce_stubs/`) | 32 stub libraries. `libSceAudioOut.c`: symbol names only, generated from firmware exports (`sceAudioOutInit`, `sceAudioOutOpen`, `sceAudioOutOutput`, `sceAudioOutClose`, `sceAudioOutSetVolume`, `sceAudioOut2…`, chat-related `sceAudioOut2EnableChat`, …). **No `libSceAudioIn`, no `libSceVoice`.** No audio headers, no audio sample. `libScePad` exports one microphone-related name (`scePadSetVibrationTriggerEffectWeakWhileEmbeddedMicInUse`), which says nothing about capture. |
| ps5-payload-dev/SDL `ee4c47d` (`src/audio/ps5/`) | A working PS5 homebrew audio *output* driver: `sceAudioOutInit()`, `sceAudioOutOpen(userId=0xFF system, type=0 main, index=0, len 256–2048 in steps of 256, freq 48000, param=format)`, formats S16 mono 0 / stereo 1, float mono 3 / stereo 4, blocking `sceAudioOutOutput`, `sceAudioOutClose`. No capture (`OnlyHasDefaultOutputDevice`). |
| ps5-payload-dev/pacbrew-repo (ports: SDL2, ffmpeg, games) | No `sceAudioIn` use anywhere. |
| PS4 homebrew headers (for example the OpenOrbis toolchain) | Declare a PS4 capture API (`sceAudioInOpen`, `sceAudioInInput`, `sceAudioInClose`, …). **That is the PS4**; nothing shows the PS5's `libSceAudioIn` has the same functions, arguments or behaviour. The audio lab only looks these names up. |

Not used, on purpose: Sony's PSN party/voice services (no unauthorized access), kernel patches,
firmware offsets, process injection, controller ownership changes.

## Research questions

| Topic | What is known |
| --- | --- |
| `libSceAudioIn` | Not in the pinned SDK. No verified PS5 declaration. Whether the firmware exports PS4-like names can be checked read-only with the audio lab (prototype A); using them would require a verified declaration first. |
| `libSceAudioOut` | Symbols in the SDK stub; declarations from the SDL driver, which plays audio from homebrew payloads. Whether a *background* companion like AKENO gets audible output has not been tested. |
| Controller / headset microphone capture | Unknown. The controller microphone and the headset jack are managed by the system (and used by party chat and games). Taking them over is out of bounds. |
| Headset output | The SDL driver opens the *main* port (TV/HDMI or whatever the system routes main audio to). The SDK stub also lists `sceAudioOutSetPadJackVolume`, `sceAudioOut2EnableChat`, … with no declarations; not used. |
| Device enumeration | No declared interface for listing audio devices. `sceAudioOutGetPortState` / `sceAudioOut2GetSpeakerInfo` exist as names only. |
| Permission and ownership | Unknown. Port limits and how the system arbitrates ports between a game and a payload are undocumented. |
| Audio thread lifetimes | `sceAudioOutOutput` blocks per grain (256 samples at 48 kHz ≈ 5.3 ms), so output needs its own thread; the audio lab uses one worker thread per test and closes the port afterwards. |
| Background audio while a game runs | Unknown (prototype E). Notifications working during games says nothing about audio. |
| Rest mode | Unknown; not examined. |
| Controller headset routing | Unknown. |

## Prototypes

| Prototype | Status |
| --- | --- |
| **A** — enumerate a microphone, show it in diagnostics | **Partially implemented in the audio lab, not run on hardware.** Real enumeration is not possible without a microphone API. The lab instead reports, read-only: which audio-related library files exist in `/system/common/lib`, whether `libSceAudioOut.sprx` and `libSceAudioIn.sprx` load, and whether they export the expected names (looked up, never called). |
| **B** — local microphone capture with explicit permission | **Not implemented: blocked.** No SDK interface and no verified PS5 declaration; calling a PS4 signature on a PS5 would be guessing. Next step if A shows `libSceAudioIn` exports: find a credible PS5 implementation or documentation of its declarations before writing any capture code. |
| **C** — local playback with a verified output API | **Implemented in the audio lab, not run on hardware.** After an explicit confirmation, a one-second 440 Hz tone at −20 dBFS through `sceAudioOut` (SDL driver declarations), resolved with `dlopen` only at that moment. |
| **D** — compatibility with the voice architecture | **Not started** (needs B and C). See [Path to a PS5 party participant](#path-to-a-ps5-party-participant). |
| **E** — capture/playback while a game runs | **Not started.** Only as a controlled experiment after A and C results and your explicit approval. |

## The audio lab build

- **Separate ELF**: `akeno-network-ps5-audiolab.elf` (CI artifact folder
  `experimental-audio-lab/`). The release ELF is built without `AKENO_EXPERIMENTAL_AUDIO` and
  contains none of the audio code; rebuilt with the same build ID, it is byte-identical to the
  previous commit except for the console page script, which carries the (dormant) diagnostics
  section that only appears when the app reports the audio lab.
- **Same start-up as the release**: identical dynamic imports (checked in CI); the audio
  functions are resolved with `dlopen`/`dlsym` in `platform_ps5.c` only when the user starts a
  check, as the experimental button notifications already do. A missing library or function is
  reported as "unavailable" and nothing else changes.
- **Nothing runs on its own**: no audio code runs at start-up; each check is a button on the
  diagnostics page; the tone needs a second confirmation and plays once at a time.
- **No capture, no storage**: nothing is recorded or sent anywhere.
- Linux: `akeno-app-audiolab` (CMake) runs the same logic with simulated audio
  (`AKENO_SIM_AUDIO`, `AKENO_SIM_AUDIO_LIBDIR`).

Code: `clients/ps5/app/audio_lab.c`, `audio_lab.h`, the `AKENO_EXPERIMENTAL_AUDIO` blocks in
`platform.h`, `platform_ps5.c`, `platform_host.c`, `app_routes.c`, `app.c`, and `audioLabSection`
in `app/web/app.js`.

## Hardware test plan (needs your approval first)

Only with an approved build (its SHA-256 from the CI `BUILDINFO.txt`), firmware 12.20, from the
home screen. The audio lab is a full AKENO app using the same data folder, so pairing is kept.

1. Close the regular AKENO app (Diagnostics → Exit, or reboot), then start
   `akeno-network-ps5-audiolab.elf` the same way as the regular ELF.
2. Check that AKENO works as usual (page opens, notifications, friends). If not, stop and
   report; return to the release ELF.
3. **Prototype A:** Diagnostics → *Check audio libraries*. Photograph the result (files found,
   which libraries load, ✓/✗ per function).
4. **Prototype C:** *Play a test tone* → *Play*. Report: the result line, whether a short tone
   was heard, and where (TV speakers, headset on the controller, USB headset). Repeat once with
   a headset plugged into the controller.
5. Exit the audio lab and start the regular AKENO ELF again.

Not part of this plan (each needs a separate approval): playing the tone while a game runs
(prototype E), firmware 13.60, any microphone access.

If anything behaves unexpectedly: exit AKENO and restart the console normally; the lab makes no
persistent changes.

## Path to a PS5 party participant

Even with working capture and playback, a PS5 participant needs a media path into the LiveKit
room. Options for later, none implemented:

1. **Native WebRTC client in the payload** (ICE, DTLS-SRTP, Opus, LiveKit signalling): libopus
   and libsrtp are BSD-licensed and mbedTLS is already used, but this is a large port with a
   real-time thread per direction; highest effort, lowest latency.
2. **Server-side bridge**: the console sends and receives Opus over the existing authenticated
   WSS connection to the AKENO server, and a small bridge joins the LiveKit room as that user
   (LiveKit server SDK). Much simpler on the console; adds one server component and a little
   latency. This is the preferred first step once B and C work.

Either way the AKENO server keeps authorizing (same party membership and token rules), and the
PS5 stays an optional participant: "capability-dependent until proven".
