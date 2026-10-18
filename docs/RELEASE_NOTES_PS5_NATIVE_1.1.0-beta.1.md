# AKENO NETWORK Native 1.1.0-beta.1 — release notes

**Beta, pre-release. Not run on a PS5.** Everything below is tested on PCs (unit, integration,
end to end with the real AKENO server, a real LiveKit server and the voice gateway). The first
console run is the owner's hardware test
([HARDWARE_TEST_PLAN_PS5_NATIVE_1.1.0-beta.1.md](HARDWARE_TEST_PLAN_PS5_NATIVE_1.1.0-beta.1.md)).

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

- The native-IPC daemon of this package (native interface 3).
- The AKENO server with voice **and the voice gateway** (`VOICE_GATEWAY_URL`, compose `voice`
  profile, docs/VOICE.md). Without it the Party screen shows that voice is not available.
- The console's "parties" permission (granted at pairing).

## Known limits

- The PS5 microphone (libSceAudioIn) is untested on 12.20; if the system refuses it, AKENO Native
  shows the exact error and voice continues for listening.
- Voice runs while AKENO Native is in front; starting a game (which closes AKENO Native through the
  launcher, or puts it in the background) ends or pauses voice. Voice during games is research.
- Everything from 1.0.0-beta.1 applies (also not yet run on a PS5).

## Fallbacks in the package

Native 1.0.0-beta.1 (without voice, with its IPC-2 daemon), Native 0.2 preview, the
hardware-tested Native 0.1 and Alpha 0.5 RC daemon — all rebuilt from their commits and
byte-verified.
