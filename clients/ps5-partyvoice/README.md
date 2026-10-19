# AKENO Party Voice (EXPERIMENTAL)

`akeno-party-voice.elf`: a background payload that keeps the PS5 in its AKENO party's voice chat
while a game runs. The AKENO plugin (AKEN00001 1.20, onionHEN Toolbox, L2 + R3) controls it; the
audio pipeline lives only here, never in ShellUI, the plugin or the game.

```
AKEN00001 (plugin) --127.0.0.1:8646, daemon's native key--> akeno-party-voice
akeno-party-voice  --voice pass (IPC 3)--> AKENO daemon --> AKENO server
akeno-party-voice  --wss://<server>/voice-gateway, Opus--> voice gateway --> LiveKit <-- PC / phone
```

**0.2.0 has not run on a PS5.** 0.1.0 did (FW 12.20): network voice worked, the output was accepted
but not heard, the microphone was refused (`0x809B0081`). 0.2.0 measures the port clock correctly,
runs the test sound through the speech pipeline and checks it in the submitted buffer, does not
repeat refused calls, and records whether it runs as a background process. Analysis and the
controlled hardware test: [docs/PS5_VOICE_AUDIO_0.2.md](../../docs/PS5_VOICE_AUDIO_0.2.md); design:
[docs/PS5_PARTY_VOICE.md](../../docs/PS5_PARTY_VOICE.md); German owner guides in
[docs/shellui/voice-alpha-0.2/](../../docs/shellui/voice-alpha-0.2/).

| File | What |
| --- | --- |
| `src/pv_engine.c` | sessions, controls, main-loop tick (heartbeat, game activity, notifications, report) |
| `src/pv_net.c` | voice pass through the daemon, TLS WebSocket to the gateway, reconnects (AKENO Native's steps) |
| `src/pv_audio.c` | playback (jitter -> Opus -> output; port clock probe, paced by the port), capture (only after consent; refusals classified, not repeated), test sound through the speech pipeline |
| `src/pv_control.c` | the local control interface (protocol 1) |
| `src/pv_status.c` | status document and the privacy-safe report |
| `src/pv_backend_ps5.c` | the only file with console functions; `src/pv_backend_host.c` simulates a console |
| `console/check-partyvoice.sh` | import gate for the PS5 ELF |

Reused unchanged: AKENO Native's voice core (`voice_codec`, `voice_dsp`, `jitter`, `voice_proto`,
`voice` reconnect policy), its daemon client (`ipc.c`, `account.c`, `social.c`), the daemon's
TLS/WebSocket client and local HTTP parser (`clients/ps5/src`, `ui_http.c`), the Voice Lab's PCM
helpers (`lab_pcm.c`), libopus 1.5.2 and mbedTLS 3.6.7.

```sh
clients/ps5-native/scripts/fetch-deps.sh
cmake -S clients/ps5-partyvoice -B clients/ps5-partyvoice/build -DPV_SANITIZE=ON && cmake --build clients/ps5-partyvoice/build
ctest --test-dir clients/ps5-partyvoice/build --output-on-failure
node clients/ps5-partyvoice/tests/e2e/run.mjs --voice clients/ps5-partyvoice/build/akeno-party-voice \
  --daemon clients/ps5/build-asan/akeno-app-native-ipc --companion clients/ps5-ingame/build/akeno-ingame-check --livekit <livekit-server>
PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk make -C clients/ps5-partyvoice/console && clients/ps5-partyvoice/console/check-partyvoice.sh clients/ps5-partyvoice/build-ps5
```

Sources MIT (AKENO). The PS5 ELF links the PS5 payload SDK runtime (GPLv3+), libopus (BSD-3-Clause)
and mbedTLS (Apache-2.0).
