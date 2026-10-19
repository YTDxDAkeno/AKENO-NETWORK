# AKENO Voice Alpha 0.1.0 — hardware results on firmware 12.20 (2026-10-19)

**Real hardware evidence, as relayed.** The owner tested `akeno-party-voice.elf` 0.1.0-alpha, build
**`ce06ba04c048-ps5`** (SHA-256 `4c19e42840d7fdb879b49b9ccf231a5209ec49c6b386abf4d0d259380b2fac8c`,
PR #18 push run [39200241005](https://github.com/YTDxDAkeno/AKENO-NETWORK/actions/runs/39200241005)),
with plugin AKEN00001 1.20 and the Phase 4.1 host, on a PS5 with FW 12.20. The numbers below are
the owner's report as given in the Phase 4.4 task. The files themselves (`voice(1).log`,
`report(1).txt`, `OnionHEN(5).log`, `OnionHEN_util_daemon(5).log`) are **not in the repository**
and have not been read; the owner is asked to send them with the 0.2.0 test.

| Area | Result | Status |
| --- | --- | --- |
| L2 + R3 in a running game, AKENO opens and closes, game controls, menu and game sound | working | **Verified on hardware** (unchanged Phase 4.1) |
| Voice service start, survives a running game | working | **Verified on hardware** |
| Voice pass, production server, voice gateway, LiveKit, a second participant | connected; 34 s of connected voice while a game ran | **Verified on hardware** |
| Incoming audio | 16,829 packets received, 16,824 decoded frames, 677 with sound | **Verified on hardware** (decoding) |
| Output route 1 (system user `0xFF`, port type 0, 48 kHz, S16 stereo, 1024 frames) | opened; 17,297 submissions, 689 blocks with sound, 0 failures; timing "NOT paced", near-zero call times | accepted; **not heard** |
| Output routes 2-4 (foreground user; port types 0, 2, 3) | `sceAudioOutOpen` = `0x80260011` | refused |
| AKENO's test sound | not heard, home screen and in a game | **not audible** |
| Microphone (`sceAudioInOpen`, user `0x190E8BE9`, 16 / 48 kHz) | `0x809B0081`; 11 attempts, 0 opens, 0 frames, 0 encoded, 0 sent | **refused** |
| Owner | "I hear all normal PS5 audio, including menu sounds and game audio. Only AKENO Voice is silent." | — |

Analysis and the next build: [PS5_VOICE_AUDIO_0.2.md](PS5_VOICE_AUDIO_0.2.md). In short: "NOT
paced" was an artifact of 0.1.0's timing (proven in simulation); the silence and the microphone
refusal follow the process context (background payload vs foreground app), established by the
identical calls that work in AKENO Native on the same console.
