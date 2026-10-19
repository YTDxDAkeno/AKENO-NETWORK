# AKENO Voice Alpha 0.2.0: why the background voice service is silent and gets no microphone

Status 2026-10-19: **0.2.0 is built and tested on PCs; it has not run on a PS5.** Nothing here
claims audible sound or a working microphone from the background service. This document records
the investigation after the first hardware test of Voice Alpha 0.1.0, what 0.2.0 changes, and the
controlled hardware test that decides what remains.

Labels: **[HW]** measured on the owner's PS5 (FW 12.20) · **[HW-relayed]** the owner's 2026-10-19
report as relayed in the Phase 4.4 task (the raw files `voice(1).log`, `report(1).txt`,
`OnionHEN(5).log`, `OnionHEN_util_daemon(5).log` are not in the repository and were not read) ·
**[SRC]** read in source code · **[SIM]** reproduced in the PC simulation · **[HYP]** hypothesis.

## 1. The 0.1.0 hardware test (build `ce06ba04c048-ps5`)

| Area | Result | Label |
| --- | --- | --- |
| L2 + R3, Phase 4.1 host, plugin 1.20, game controls, game and menu sound | all working, unchanged | [HW-relayed] |
| Voice pass, gateway, LiveKit, a second participant | connected; 34 s of connected voice while a game ran | [HW-relayed] |
| Incoming audio | 16,829 packets, 16,824 decoded frames, 677 frames with sound | [HW-relayed] |
| Output route 1 (`sceAudioOutOpen(0xFF, 0, 0, 1024, 48000, S16 stereo)`) | opened; 17,297 submissions, 689 blocks with sound, 0 failures; **"NOT paced"**, near-zero call times; **nothing heard**, neither party audio nor the test sound, on the home screen and in a game | [HW-relayed] |
| Output routes 2-4 (foreground user; port types 0, 2, 3) | `sceAudioOutOpen` = **`0x80260011`** for all three | [HW-relayed] |
| Microphone (`sceAudioInOpen(0x190E8BE9, 1, 0, 256, 16000 / 48000, 0)`) | **`0x809B0081`**, 11 attempts, 0 opens, 0 frames, 0 packets | [HW-relayed] |
| Normal PS5 audio | "I hear all normal PS5 audio, including menu sounds and game audio. Only AKENO Voice is silent." | [HW-relayed] |

## 2. Root cause: proven, established, and open

### 2.1 Proven: "NOT paced" was an artifact of 0.1.0's own timing [SRC] [SIM]

0.1.0 slept a block (21.3 ms) after every output call that returned within 2 ms
(`pv_audio.c`, `pace_by_clock`). A console port has a small queue: its first calls return at
once. So 0.1.0 slept, the port drained meanwhile, the next call found room again and returned at
once, and so on: every call returned quickly whether or not an audio clock consumed the port.
AKENO Native (heard), audio lab v2 and the Voice Lab submit back to back instead.

Replayed in the unit test `test_pacing_artifact` on a virtual clock against a port whose device
clock drains a two-block queue: 0.1.0's loop measures **0.9 ms per call** ("NOT paced") while the
clock consumes every block; back to back, the same port measures **20.8 ms per call** (paced). The
same port model runs in real time in the simulated console (`PV_SIM_OUT=queue`). 0.1.0's loop has a
second regime (once a call blocks for 2 ms or more it stops sleeping and stays back to back); the
owner's log, with near-zero call times for the whole session, is the first. The owner's "NOT paced"
with near-zero call times therefore says nothing about
whether the samples were played. **It does not explain the silence:** audio lab v2 submitted back
to back from a background payload on the same console, was paced, and was silent too
([PS5_AUDIO_RESEARCH.md](PS5_AUDIO_RESEARCH.md)).

### 2.2 Established by controlled comparison: the process context decides [HW]

| The same call | Foreground app (AKENO Native, `FAKE00000` from the homebrew launcher) | Background payload (ELF loader) |
| --- | --- | --- |
| `sceAudioOutOpen(0xFF, 0, 0, 1024, 48000, 1)` + `sceAudioOutOutput` | **heard**, four tone tests (Native 0.1, 2026-10-17) | **silent**: audio lab v1 (accepted), audio lab v2 (accepted, paced, home screen too), voice 0.1.0 (accepted, home screen and game) |
| `sceAudioInOpen(user, 1, 0, 256, 16000, 0)` via `dlopen(libSceAudioIn.sprx)` | **opened**, level followed speech (Native 1.1, 2026-10-18) | **`0x809B0081`** (voice 0.1.0) |
| `scePadOpen` | **opened** (Native 0.1) | **`0x809B0081`** (AKENO daemon, Alpha 0.5; another payload on 13.60, AnyPS5 #3177) |

Same console, same firmware, same user, byte-identical arguments, same libraries. The only
difference is the kind of process. That is as close to proof as is possible without the console's
source: **on 12.20 a background payload is neither heard nor given the microphone; a foreground
application is.** The external evidence agrees: Omega's daemon README ("on the PS5 a background
payload has no audio session: `sceAudioOut` opens but nothing is heard … apps launched as bigapp …
do play"), and every PS5 homebrew with audible sound or a working pad found so far runs as the
foreground app ([PS5_NATIVE_ADR.md](PS5_NATIVE_ADR.md), E6-E8).

### 2.3 What the codes mean (checked against sources, not guessed)

| Code | From | Meaning | Source and strength |
| --- | --- | --- | --- |
| `0x809B0081` | `sceAudioInOpen` (voice 0.1.0), `scePadOpen` (daemon) | **No public meaning.** It is not an AudioIn code (those are `0x802601xx`: `0x80260101` invalid handle … `0x8026010C` system IPC) and not a pad code (`0x8092xxxx`). The same code from two different device libraries, in background processes only, while both devices open in the foreground app: a shared, process-level refusal outside both libraries | shadPS4 `audioin_error.h`; [QUICK_ACCESS.md](QUICK_ACCESS.md); web search found no definition |
| `0x80260011` | `sceAudioOutOpen` with the foreground user (routes 2-4) | PS4 SDK name `SCE_AUDIO_OUT_ERROR_SYSTEM_RESOURCE` | shadPS4 `audioout_error.h` (PS4-derived); not measured from a foreground app, so whether it is process-specific is **open** |
| `0x8026000E` | `sceAudioOutInit` | already initialised; treated as success | passthrough sample (hardware), unchanged |

**[HYP]** The mechanism is a per-process audio and device context that the system creates for
the foreground application (and its user) and not for a background payload. Which system
component enforces it is not documented and is not needed for the decisions below.

### 2.4 Ruled out as causes of the silence

| Candidate | Why it is ruled out |
| --- | --- |
| Format, block size, rate | lab v2 tried S16/float, mono/stereo, 256-2048 frames: all paced, all silent; route 1 is Native's audible call |
| Pacing / submission speed | lab v2 back to back: paced and silent (2.1) |
| PCM conversion, interleaving, volume, frame counts | unit tests (`test_pcm_format`); 0.2.0 verifies the 880 Hz test tone, left = right, in the very buffer handed to `sceAudioOutOutput` |
| Buffer alignment or lifetime | 0.2.0 uses 64-byte aligned buffers that live for the port's lifetime, like Native's audible test |
| The test sound not reaching the port | 0.1.0 mixed it after the decoder; 0.2.0 runs it through Opus, the jitter buffer and the decoder and checks every submitted block |
| Game audio competing | silent on the home screen too |
| Wrong user | route 1 uses the system user exactly like Native; the foreground user's ID came from `sceUserServiceGetForegroundUser` (`0x190E8BE9`), never guessed |

## 3. Foreground (works) vs background (fails): the comparison

| # | Aspect | AKENO Native (foreground, heard, mic works) | Voice service 0.1.0 (background) | 0.2.0 |
| --- | --- | --- | --- | --- |
| 1 | Process type, context | the foreground application: the launcher closes the game, launches `FAKE00000` for the foreground user and replaces its image with the ELF (websrv `hbldr`: kill, ptrace, privilege raise) | a background payload spawned by the ELF loader (no app ID), survives games | unchanged; now **measured** every run (`sceKernelGetAppInfo` + running app, Native's `context.c`) |
| 2 | Audio initialisation | `sceAudioOutInit` once (`0x8026000E` = ok) | same | same |
| 3 | Audio libraries | `libSceAudioOut` linked, `libSceAudioIn.sprx` by `dlopen` | same | same |
| 4 | User | output: system user `0xFF`; mic: foreground user | output: `0xFF` (route 1) or foreground user (routes 2-4); mic: foreground user | route 1 `0xFF`; route 2 control; routes 3-4 retired |
| 5 | Input parameters | type 1, index 0, 256 frames, 16 kHz (48 kHz fallback after any failure), S16 mono | same | same; 48 kHz only after a **rate** refusal |
| 6 | Output parameters | port type 0, index 0, 1024 frames, 48 kHz, S16 stereo | same (route 1) | same |
| 7 | Port lifecycle | open per voice session/test; close at the end | open per session/route/test; close at the end | same, plus a 12-block silent clock probe at open and a 2-block silence tail at close |
| 8 | Buffers | heap / 64-byte static, 2 x 1024 S16 interleaved | heap (16-byte), same layout | 64-byte aligned heap, same layout |
| 9 | Timing | back to back; the blocking call paces the thread | slept a block after any quick call (2.1) | back to back; sleeps only when > 6 blocks ahead of real time (a discarding port) |
| 10 | Output device | the system routes the app's main port (TV/HDMI or headset) | the same call; nothing reaches a device | no routing function is used (none has a verified declaration; out of bounds) |
| 11 | Microphone device | the foreground user's DualSense/headset mic | refused (`0x809B0081`) | same call; refusal classified and not repeated in the same situation |
| 12 | PS5-specific requirements | an application process with the user's audio/device context, granted by the system to the foreground app | missing | cannot be obtained legitimately (section 5) |
| 13 | Foreground vs background behaviour | runs only while no game runs (starting one ends it) | runs during games, silent, no mic | same; the report says so |

What the foreground runtime provides that the background process lacks: **being the foreground
application process** (with the user's audio session and device binding). The homebrew launcher
creates it by replacing a launched app's process. AKENO itself never does that (ADR D1), and doing
it for the voice service would close the running game, which defeats the purpose.

## 4. What 0.2.0 changes (`clients/ps5-partyvoice`)

- **Timing (2.1):** a bounded clock probe at every port open (12 silent blocks back to back,
  256 ms, inaudible either way): "consumed by an audio clock", "returns at once" or "undecided";
  then the port's own blocking paces the thread; a real-time guard sleeps only when the port runs
  more than six blocks ahead (bounded CPU for a port that discards). The report shows time inside
  calls vs audio carried, and how often the guard slept.
- **Test sound through the speech pipeline:** N beeps of 880 Hz on route N, -20 dBFS at 100 %
  volume, generated, Opus-encoded, put through a jitter buffer, decoded by the same decoder code as
  party speech, then the same volume, mono-to-stereo and output call. Outside voice it uses its own
  port (test B); in voice it is mixed in before the volume. Every submitted block is checked: tone
  blocks, blocks measured at 880 Hz ±5 %, left = right, peak level. A refused port means "NOT
  PLAYED", never "mixed into the output"; at 0 % volume or without a port the request is refused.
- **Microphone:** the code is classified (`process`, `rate`, `argument`, `busy`, `no-user`,
  `no-library`); 48 kHz is tried only after a rate refusal; a refusal is remembered for the same
  user and running app and the call is **not repeated** (one new attempt when a game starts or
  ends, so each test situation gets exactly one); at most 8 `sceAudioInOpen` calls per run.
  Consent is unchanged: off at every join, turned on only by the user, never stored.
- **Routes:** route 2 (foreground user) stays as the single control and is not reopened in the
  same situation after `0x80260011`; routes 3 and 4 (PS4 port numbers) are retired without a
  console call. Plugin 1.20 still lists them; pressing one shows "Route 3 is retired …".
- **Process context:** recorded at start, at every join and test sound, and when the running app
  changes.
- **Report** (`/data/akeno-party-voice/report.txt`): eight separate lines - NETWORK CONNECTED,
  REMOTE AUDIO RECEIVED, AUDIO SUBMITTED, AUDIO AUDIBLE (always "NOT CONFIRMED"), MICROPHONE OPEN,
  MICROPHONE CAPTURING, MICROPHONE TRANSMITTING, REMOTE USER HEARD MICROPHONE (always "NOT
  CONFIRMED") - and a [Diagnosis] section that states only what the measurements imply.
- **Status document:** protocol 1 unchanged (plugin 1.20 works as is); new members `test`,
  `process`, `mic.refusal`, `output.probe` and the eight evidence entries (`audioAudible` and
  `remoteHeardMic` are always `"unconfirmed"`).
- **Imports:** one new, weak, read-only system import (`sceKernelGetAppInfo`, already imported by
  AKENO Native on 12.20); everything else identical to 0.1.0 (`check-partyvoice.sh`).

Not changed: the Phase 4.1 host, its hooks and `config.ini`; plugin 1.20 (rebuilt byte-identical,
SHA-256 `f3fdf510…`); the daemon; the voice protocol; the server, gateway and LiveKit.

## 5. Background audio: the questions, answered

| Question | Answer |
| --- | --- |
| Does a background payload have permission to own an output port? | It may **open** one (system user, main port) and submit to it; it is not heard (2.2). With the foreground user it gets `0x80260011` |
| Does a port opened as the system user give accepted-but-inaudible output? | **Yes**, from a background payload on 12.20 (lab v1, lab v2, voice 0.1.0). From the foreground app the same call is audible |
| Is microphone access restricted to an audio-capable foreground application? | On 12.20, **yes as far as measured**: the identical call opens in the foreground app and returns `0x809B0081` in the background service |
| Does the audio subsystem need application registration or lifecycle state? | Evidently a foreground application process; the exact requirement is undocumented [HYP] |
| Is the payload using the wrong user or process context? | The user is right (system user exactly as Native; the foreground user from the system). The **process** context is what differs, and the payload cannot change it legitimately |
| Is there a supported way to keep an audio session independently of the AKENO menu? | None found. The libraries have focus/attach functions (`sceAudioOutGetFocusEnablePid`, `sceAudioOutAttachToApplicationByPid`, `scePadSetProcessFocus`, …) with **no public declaration**; using them would make the system treat AKENO's audio as another app's (spoofing). Excluded, never called or named (CI) |
| Can the native runtime offer audio services to the background client? | Technically yes **while AKENO Native is the foreground app**, but then no game runs (the launcher closes the game to start Native), and Native already has its own two-way party voice. It cannot help during a game, so it is not built |
| Other routes | Code inside ShellUI (it is audible during games) would mean changing the validated Phase 4.1 host and running audio code inside a Sony system app; code inside the game is injection. Both are out of bounds for this phase |

**Conclusion (pending the 0.2.0 hardware test):** with the interfaces that are legitimately
available, a background payload on FW 12.20 can join the voice chat and decode it, but cannot be
heard and cannot capture the microphone. **Two-way voice works where the platform allows it:**
AKENO Native as the foreground app, with no game running. The server step that blocked it on
2026-10-18 (the voice pass, HTTP 404) now answers: the background service got its pass through the
same daemon route and connected on 2026-10-19. Native's own voice against the production gateway is
what test A checks. During a game, the closest safe
option is voice on a phone or PC in the same party (the AKENO web app joins LiveKit directly).

## 6. The controlled hardware test (owner, FW 12.20)

German guide: [shellui/voice-alpha-0.2/HARDWARE-TEST-DE.md](shellui/voice-alpha-0.2/HARDWARE-TEST-DE.md).

| Test | What | Decides |
| --- | --- | --- |
| A | **Foreground reference** with the installed AKENO Native 1.1.0-beta.1: audio test tone, microphone test, then party voice with a friend (voice service left first) | that the console, the account and the server give two-way voice in the foreground today |
| B | Voice service 0.2.0, home screen, not in voice: test sound route 1 (one beep) | probe verdict and tone verification for a background port; heard or not |
| C | Voice service in a party with a friend talking (home screen) | remote audio decoded and submitted; heard or not |
| D | Microphone, once, after consent | the exact code; the second press must say "not tried again" without a new call |
| E | B and C on the home screen | as above |
| F | Only after E: start a game, repeat B, C, D once | the same in a game; game stays controllable and audible |
| G | L2 + R3 several times while the service is in voice | no freeze, no ShellUI problem |

| Result | Meaning | Next step |
| --- | --- | --- |
| A two-way OK; B probe "consumed", tone verified, **not heard**; D `0x809B0081` | the background-process limit, confirmed with clean timing | in-game voice through a phone/PC; console voice in AKENO Native; the background service stays an experiment |
| B probe "returns at once" | the background port is not even consumed | same conclusion, stronger |
| B or C **heard** | contradicts 2.2: keep the build, longer session, then latency/echo | |
| D opens and captures | contradicts 2.2 for the mic: talk-only while gaming becomes possible | |
| A fails | a console, account or server problem first; the background results wait | |

Send back: `/data/akeno-party-voice/report.txt`, `report-previous.txt`, `voice.log`,
`/data/akeno-native/native-report.txt` and `native.log` (test A), and the filled-in answers.

## 7. Tests (PC; never evidence of PS5 sound)

`clients/ps5-partyvoice/tests/test_partyvoice.c`: classification of every code above; the clock
verdict; the 0.1.0 pacing artifact replayed against a queue port; PCM format and an Opus round trip
of the test beep; the process check (background / foreground / unknown); the test sound outside
and inside voice with tone verification at the submit point; the refusal policy (argument,
process, rate, library, read failures, the per-run limit, a new attempt after a game change);
route 2 refused like on the PS5 and not repeated; retired routes; reports and the status document.
ASan/UBSan, TSan and clang-tidy clean. `tests/e2e/run.mjs` runs the real server, LiveKit, the
gateway, the daemon, the plugin's code and a PC participant, including test B and the refusal
messages as the plugin shows them.
