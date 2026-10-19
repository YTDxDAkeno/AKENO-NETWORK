# Party voice while a game runs: architecture, evidence and the Voice Lab (EXPERIMENTAL)

Status (2026-10-18): **not working and not claimed.** AKENO Native 1.1 has working party voice in
the foreground app. Voice while a game runs needs a process that survives the game and can use
the microphone and speaker from the background. Nobody has measured that on a PS5 yet. The Voice
Lab (`clients/ps5-voicelab`) is the measurement.

> **Update 2026-10-19 (Phase 4.3):** on the owner's decision the voice service was built before the
> Voice Lab ran: `akeno-party-voice.elf` ([PS5_PARTY_VOICE.md](PS5_PARTY_VOICE.md)), with the Voice
> Lab's four output variants as selectable speaker routes, a test sound per route and the same
> measurements in its report. Its hardware test answers the Voice Lab's questions inside a real
> party. Still **not run on a PS5**; nothing below is changed by it.

## Why the foreground app cannot do it

| Fact | Evidence |
| --- | --- |
| The homebrew launcher starts AKENO Native as the foreground application (BigApp). It first closes the running game (`sceSystemServiceKillApp`), launches the placeholder app `FAKE00000` and replaces its code with the ELF | websrv `7748df9`, `src/ps5/hbldr.c` (source read) |
| So AKENO Native and a game never run at the same time; starting a game from the home screen ends AKENO Native | the same; Native 0.1 hardware test (FW 12.20) |
| An entry with `daemon: true` is started differently: `sys_launch_daemon` → `elfldr_spawn`, a background payload, no app is closed | websrv `7748df9`, `src/websrv.c:186`, `src/ps5/sys.c:260` (source read) |
| The AKENO daemon is such a background payload (SceSpZeroConf under elfldr). It survives games | Alpha 0.5 hardware results |
| Audio **output** from a background payload: accepted, consumed at playback speed, **not heard** (home screen and browser) | audio lab v2, FW 12.20 ([PS5_AUDIO_RESEARCH.md](PS5_AUDIO_RESEARCH.md)) |
| Microphone capture works in the foreground app (`sceAudioIn*` via `libSceAudioIn.sprx`, 16 kHz, level followed speech) | Native 1.1.0-beta.1, FW 12.20 ([PS5_NATIVE_1.1_HARDWARE_RESULTS_FW12.20.md](PS5_NATIVE_1.1_HARDWARE_RESULTS_FW12.20.md)) |
| Microphone capture from a background payload | **never tested** |
| Audio output from a background payload **while a game runs**, or with the foreground user's ID or other port types | **never tested** |
| The controller from a background payload: `scePadOpen` = `0x809B0081` | Alpha 0.5 hardware results |

A game-time voice path therefore has to live in a background payload, and the open questions are
exactly the ones the earlier tests left out.

## Target architecture (only if the Voice Lab shows it is possible)

```
AKENO Native (foreground app, when open)         Game (foreground)
   │ controller: join/leave party, mute,             │
   │ volume, device state                            │
   ▼ 127.0.0.1 IPC                                    │
AKENO voice service (background payload, elfldr) ────┘ keeps running during the game
   ├─ microphone: sceAudioIn (16 kHz) → DSP → Opus → party transport (existing voice_proto)
   ├─ speaker: party mix → sceAudioOut (variant the lab proved audible)
   └─ notifications for join/leave/mute (sceKernelSendNotificationRequest, already verified)
```

- The service would reuse AKENO Native's voice core (`voice_dsp`, `voice_codec`, `voice_proto`,
  `jitter`) unchanged. It would be a **new** payload with its own version and hardware plan. The
  Alpha 0.5 daemon stays byte-identical.
- AKENO Native becomes a controller for it. While no game runs, Native keeps its own foreground
  voice, as in 1.1.
- Controls during a game, without the controller (blocked for payloads): system notifications,
  the companion's Toolbox page ([IN_GAME_ACCESS.md](IN_GAME_ACCESS.md), owner decision), and the
  web page.

Decision table for the Voice Lab results:

| A (mic in game) | B (a tone heard in game) | C (echo heard) | Next step |
| --- | --- | --- | --- |
| SOUND | yes (variant n) | yes | Build the voice service with variant n. Hardware plan: 10-minute party in a game |
| SOUND | no | no | **Talk-only** mode: game-time voice is sent, party audio cannot be heard in the game. Shipped only as clearly labelled "talk only" if the owner wants it |
| ALL_ZERO / OPEN_FAILED | yes | – | **Listen-only** mode: hear the party during a game, no microphone |
| ALL_ZERO / OPEN_FAILED | no | – | No background voice on 12.20 with public interfaces. Party voice stays a foreground feature; documented as such |

A successful call is never treated as working sound. Every "yes" above is the player's answer in
the report, backed by the measured PCM statistics.

## The Voice Lab (`clients/ps5-voicelab`)

A separate background payload. It does not change the daemon or AKENO Native. One run takes about
3–7 minutes and follows a plan, announced through system notifications:

| Test | What happens | Measured |
| --- | --- | --- |
| **A** microphone, home | 6 s capture after "speak now" | reads, errors, RMS dBFS, peak, 20 ms windows above −45 dBFS, longest zero run → `SOUND` / `QUIET_SIGNAL` / `CONSTANT` / `ALL_ZERO` / `NO_SAMPLES` |
| **B** output, home | four beep sequences, one per variant: 1 beep system user + main port; 2 beeps foreground user + main; 3 beeps foreground user + type 2; 4 beeps foreground user + type 3 | open/output codes, ms per call (paced = consumed by an audio clock); **heard: you fill in which counts you heard** |
| wait for a game | "start a game now"; waits up to 300 s until the running app ID changes, then 30 s for the game to load | app IDs |
| **A, B** in game | the same tests while the game runs (8 s capture) | as above |
| **C** full duplex | 10 s: microphone → 400 ms delay → speaker (the lab's own process, foreground user, main port) | both directions, missing/dropped samples; **heard your own voice?** |
| **D** survival | a 250 ms heartbeat thread for the whole run; every 10 s `GET /api/instance` on the daemon (127.0.0.1:8642) and a TCP connect to the AKENO server from `/data/akeno/settings.json` (read only) | largest heartbeat gap per phase (seconds = the process was paused), probe successes and latency |
| **E** game + AKENO audio | during B and C in game | **was the game still audible?** |

Test **F** (foreground/background transitions of AKENO Native itself) is measured by AKENO Native.

Safety (enforced by `console/check-voicelab.sh` on every build):

- 15 system imports, all used before in an AKENO build on 12.20. The new parts are the calls'
  **context** (background, during a game), the foreground user's ID for output, and port types 2
  and 3. A refused port is reported, and nothing else is attempted.
- Run-time loading: only `libSceAudioIn.sprx` with `sceAudioInOpen/Input/Close`, exactly as
  AKENO Native 1.1.
- No app launching or killing, no mounting, ptrace, kernel helpers, registry, pad, focus, routing,
  volume or system setting. Read only from `/data/akeno/`. Writes only `/data/akeno-voicelab/`.
- Nothing is recorded or sent. Microphone blocks are reduced to statistics, and in test C they are
  played back once. Beeps are at −12 dBFS.
- It refuses to run twice (it holds 127.0.0.1:8644), and ends by itself.
- Risk: as with any payload, a crash of the lab ends only the lab. If the game stutters or the
  audio misbehaves, closing the game or restarting the console ends everything; nothing persists.

### Hardware test plan (FW 12.20)

1. Keep the AKENO daemon running as usual (test D probes it; it also runs without it).
2. Put the AKENO Voice Lab folder (`AKENOVoiceLab/`: `akeno-voicelab.elf` + `homebrew.js`) next to
   AKENO Native in the launcher's homebrew folder, or send `akeno-voicelab.elf` to the ELF loader
   (port 9021) like the daemon. Use a headset on the DualSense if you can (the mic is the
   DualSense/headset input; note which you used).
3. Start it **from the home screen**. Follow the notifications: speak when asked, count the beeps.
4. When asked, start a game and play (any game with continuous sound). Speak and count beeps again,
   then speak during the echo test.
5. When "finished" appears, open `/data/akeno-voicelab/report.txt` (FTP, or the launcher's file
   view). Fill in every `____`: which beep counts you heard at home and in the game, whether you
   heard your own voice, whether the game sound stayed. Send the report back.

Interpretation: the `[Summary]` lines are the result. `A.game.mic=SOUND` with speech, `ALL_ZERO`
when silent means the microphone delivers real audio to a background process during a game.
`B.game.toneN heard=yes` names the output variant to use. Large `D.game.heartbeat_gap_max_ms`
means the system pauses background payloads during games.

### Build

```
cmake -S clients/ps5-voicelab -B clients/ps5-voicelab/build -DAKN_LAB_SANITIZE=ON && cmake --build clients/ps5-voicelab/build
clients/ps5-voicelab/build/akn_lab_tests                       # unit tests and simulated runs
AKN_LAB_QUICK=1 clients/ps5-voicelab/build/akeno-voicelab /tmp # a simulated run, report on stdout
PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk make -C clients/ps5-voicelab/console package
clients/ps5-voicelab/console/check-voicelab.sh clients/ps5-voicelab/build-ps5
```
