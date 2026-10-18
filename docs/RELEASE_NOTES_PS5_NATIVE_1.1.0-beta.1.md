# AKENO NETWORK Native 1.1.0-beta.1 — release notes

**Beta, pre-release.** Everything below is tested on PCs (unit, integration, end to end with the
real AKENO server, a real LiveKit server and the voice gateway). On a PS5 with firmware 12.20
(build `196ca13c71d1-ps5`, owner report 2026-10-18) the app starts, connects to the protocol-3
daemon, and the **microphone test works** (libSceAudioIn opens at 16 kHz, the level follows
speech, the microphone closes). **Two-way voice has not run on a PS5 yet**: the server side is not
deployed ([PS5_NATIVE_1.1_HARDWARE_RESULTS_FW12.20.md](PS5_NATIVE_1.1_HARDWARE_RESULTS_FW12.20.md)). Test plan:
[HARDWARE_TEST_PLAN_PS5_NATIVE_1.1.0-beta.1.md](HARDWARE_TEST_PLAN_PS5_NATIVE_1.1.0-beta.1.md).

## New: two-way party voice

- **Join voice** on the Party screen: you hear your party and they hear you, together with the
  friends on the website and the apps (one LiveKit room).
- Mute, deafen, the others' volume and your microphone volume; speaking indicators; reconnects
  after a network loss; voice ends at once when you leave the party, are removed, or the console
  loses the "parties" permission.
- **Microphone test** in Settings (local only).
- The microphone opens only after you confirm; nothing is recorded or stored.
- Opus (libopus 1.5.2) at 32 kbit/s, 20 ms; jitter buffer and loss concealment.

## Requirements

**Server deployment required first:** the device voice pass and the voice gateway are new server
code that is not deployed anywhere yet ([DEPLOYMENT_REVIEW_PS5_VOICE.md](DEPLOYMENT_REVIEW_PS5_VOICE.md)).


- The native-IPC daemon of this package (native interface 3).
- The AKENO server with voice **and the voice gateway** (`VOICE_GATEWAY_URL`, compose `voice`
  profile, docs/VOICE.md). Without it the Party screen shows that voice is not available.
- The console's "parties" permission (granted at pairing).

## Known limits

- The PS5 microphone (libSceAudioIn) opened and captured speech on 12.20 in the local test; network
  voice with it is untested. If a system refuses the microphone, AKENO Native shows the exact error
  and voice continues for listening.
- Without the server's device voice pass, joining voice ends with "The requested endpoint was not
  found." (HTTP 404): deploy the server side first.
- The microphone test's result is kept in `mic-test-last.txt` (and `mic-test-last-ok.txt`) and shown
  in every report, also after an accidental background start (from commit `96a5fe6`).
- Voice runs while AKENO Native is in front; starting a game (which closes AKENO Native through the
  launcher, or puts it in the background) ends or pauses voice. Voice during games is research.
- Everything from 1.0.0-beta.1 applies (also not yet run on a PS5).

## Fallbacks in the package

Native 1.0.0-beta.1 (without voice, with its IPC-2 daemon), Native 0.2 preview, the
hardware-tested Native 0.1 and Alpha 0.5 RC daemon — all rebuilt from their commits and
byte-verified.
