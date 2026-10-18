# AKENO NETWORK Native 1.1.0-beta.1 — hardware results on firmware 12.20

**Real hardware evidence.** Reported by the console owner on 2026-10-18: PS5, firmware **12.20**,
kstuff, the existing websrv homebrew launcher. Only what was reported is recorded here; anything
not mentioned stays **unverified**.

## Builds

| Component | Build | Identified by |
| --- | --- | --- |
| AKENO NETWORK Native | **`196ca13c71d1-ps5`** (1.1.0-beta.1, commit `196ca13`, push run [39095870564](https://github.com/YTDxDAkeno/AKENO-NETWORK/actions/runs/39095870564), `eboot.elf` SHA-256 `62976eeb51a5bce882135b1233edad0af5da89919de527cc328503d0db26819c`) | the owner's `native.log` (build ID) |
| Native-IPC daemon | **`196ca13c71d1-ps5-native-ipc`** (native interface protocol 3, SHA-256 `b61042a92b85d33f33a535e2634946dc4d82fdf7cf0b7751c299f1dd08279cc7`) | the owner's report (build ID) |

Later commits (the security fix of the voice service, the kept microphone test result) produce
new build IDs that have **not** run on a PS5.

## Results

| Area | Test plan | Result on 12.20 | Status |
| --- | --- | --- | --- |
| Start from the launcher | V0.2 | AKENO Native 1.1 launched | **Verified on hardware** |
| Native interface 3 | V0.1, V0.3 | The daemon reported "native interface protocol 3 ready"; AKENO Native connected to it | **Verified on hardware** |
| Daemon realtime and party | V2.1 | Server realtime connection established; joining a party worked | **Verified on hardware** |
| Microphone library | V1.1 | `libSceAudioIn.sprx` loaded with dlopen after the user confirmed | **Verified on hardware** |
| Microphone open | V1.1 | `voice.mic: Microphone open (sceAudioIn via dlopen(libSceAudioIn.sprx) (PS5), 16000 Hz, 256 frames per read)` | **Verified on hardware** |
| Microphone test (Settings) | V1.2 | The level meter reacted to speech; louder speech gave a higher level | **Verified on hardware** (owner's observation; no frame counts were kept, see below) |
| Microphone close | V1.4 | The microphone was closed when the test ended | **Verified on hardware** |
| Audio output | V2.4 | Audible native audio was already verified with Native 0.1 ([results](PS5_NATIVE_HARDWARE_RESULTS_FW12.20.md)); not re-tested with voice | Verified earlier (0.1), voice playback **unverified** |
| Joining party voice | V2.3 | Refused by the server: `POST /parties/:id/voice/device-token` → **HTTP 404 NOT_FOUND**; AKENO Native showed "Voice stopped: The requested endpoint was not found." | **Blocked**: the production server does not have the voice pass endpoint or the voice gateway yet |
| Two-way voice, remote playback, latency, long sessions | V2.4 and later | not possible without the server side | **Unverified** |

## What this means

- The console's microphone path works on 12.20: the library loads, the port opens at 16 kHz with
  256-frame reads, it captures speech, and it closes again. The microphone is no longer the open
  question for PS5 voice.
- The blocker for voice is server-side: the device voice pass (`/parties/:id/voice/device-token`),
  the voice gateway and `VOICE_GATEWAY_URL` must be deployed first
  ([DEPLOYMENT_REVIEW_PS5_VOICE.md](DEPLOYMENT_REVIEW_PS5_VOICE.md)).
- Real two-way network voice, remote audio playback, latency and long-term stability remain to be
  tested once the gateway runs.

## Diagnostics gap found by this test

The owner's `native-report.txt` was later rewritten by an accidental background start (no
microphone, zero frames), while `native.log` still recorded the foreground test. Since commit
`96a5fe6` the microphone test keeps its result in `mic-test-last.txt` and `mic-test-last-ok.txt`
(counts, levels and codes only), and every report, including one written by a background start,
shows a `[Last microphone test]` section.
