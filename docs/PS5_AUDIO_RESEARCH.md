# PS5 audio: research, the silent v1 tone, and audio lab v2

Status 2026-10-17. **Audible output from AKENO on a PS5 is NOT confirmed**: audio lab v2 was tested
on firmware 12.20 and none of its tones was heard (below). The cause is **not** established yet;
the audio-focus idea below remains a hypothesis. Microphone capture is not attempted. Desktop and phone voice (see [VOICE.md](VOICE.md)) do not depend on any of this.

## What the hardware test of audio lab v1 showed

Your test of `akeno-network-ps5-audiolab.elf` (CI run 38558019000, `ae94851`) on firmware 12.20:

| Level | Result | What it proves |
| --- | --- | --- |
| 1. API calls succeed | `libSceAudioOut.sprx` loaded; `sceAudioOutInit/Open/Output/Close` resolved; `libSceAudioIn.sprx` exports `sceAudioInOpen/Input/Close` | The names exist in 12.20. Nothing about arguments, privileges or behaviour. |
| 2. Samples accepted | "accepted (7 runs)": every `sceAudioOutOutput` returned success | The system took the buffers. |
| 3. Samples played by the audio subsystem | **unknown** — v1 did not time the calls | — |
| 4. Sound reaches TV/headset | **No** (nothing heard; games are audible) | Something between 2 and 4 drops the audio. |

## What the hardware test of audio lab v2 showed (firmware 12.20, 2026-10-17)

Owner's report for `akeno-network-ps5-audiolab-v2.elf` (Alpha 0.5 RC, `7e28255`), recorded in
[PS5_HARDWARE_RESULTS_FW12.20.md](PS5_HARDWARE_RESULTS_FW12.20.md):

| Test | Result |
| --- | --- |
| Lab start, library check | started successfully |
| Tone tests with feedback (formats, frames per call, levels) | **none audible** |
| Delayed test (tone while the home screen is in front) | **not audible** |
| Browser comparison (C6) | "API missing, state unavailable, heard −1": the 12.20 browser has **neither `AudioContext` nor `webkitAudioContext`** |

Not yet known, because the per-run lines of the report were not sent: whether `sceAudioOutOpen`
and every `sceAudioOutOutput` returned success, and whether the calls were **paced** (the
"Timing" line). Those decide between the explanations below — see
[Reading the audio lab v2 report](#reading-the-audio-lab-v2-report).

What the results already exclude or weaken:

- **Not the browser being in front:** the delayed test on the home screen was silent too, so "the
  browser owns the audio focus" alone does not explain the silence.
- **Not one bad preset:** every preset and level was silent, so a single format or block-size
  mistake is unlikely (an *unsupported* format would also show as an open error in the report).
- **No browser route via Web Audio:** the system browser cannot synthesise or play PCM through Web
  Audio, so "play party audio in the page with Web Audio" (the first row of the decision table
  below) is not available on 12.20.

## Root-cause analysis

### Comparison with the PS5 SDL audio driver (ps5-payload-dev/SDL `ee4c47d`, `src/audio/ps5`)

| Aspect | SDL driver | AKENO audio lab v1 | Audio lab v2 |
| --- | --- | --- | --- |
| Initialization | `sceAudioOutInit()` once; non-zero = failure | same | same (result shown; called once per launch) |
| User ID | `0xFF` (system) | `0xFF` | `0xFF` (only verified value) |
| Port type | `0` main | `0` | `0` (only verified value) |
| Index | `0` | `0` | `0` |
| Format (`param`) | S16 mono 0 / stereo 1, float mono 3 / stereo 4; apps almost always ask for **stereo** | S16 **mono** | selectable within those four; **default S16 stereo** |
| Rate | 48000 | 48000 | 48000 |
| Frames per call | 256…2048 in steps of 256 (typically 1024/2048) | 256 | 256/1024/2048; **default 1024** |
| Buffer | heap (`SDL_calloc`, ≥16-byte aligned) | stack array | **64-byte aligned static buffer** |
| Output | blocking `sceAudioOutOutput` per block | same | same, **every call timed** |
| Volume / routing / port state | not touched | not touched | not touched (no verified declaration) |
| Close | `sceAudioOutClose` at device close | after the tone | after silence tail; result shown |
| **Process context** | **runs inside a launched application (BigApp)** | **background payload** | background payload |

The format, block size and buffer differences are plausible but weak explanations: the system
accepted every block, so it understood the format. v2 still moves to the SDL driver's common
configuration so they can be excluded.

### Leading hypothesis (not confirmed): process context and application audio focus

> Status 2026-10-17: **not confirmed and not refuted.** The v2 hardware test was silent in every
> context, including the home screen; the report's timing lines are needed before this hypothesis
> can be weighed against the alternatives listed in
> [Reading the audio lab v2 report](#reading-the-audio-lab-v2-report).

- **How SDL homebrew actually runs.** The PS5 homebrew that plays audio with this SDL driver
  (FFplay, RetroArch, ScummVM ports, …) is started by ps5-payload-dev/websrv's homebrew loader
  (`src/ps5/hbldr.c`, commit `7748df9`): it **kills the running game**, launches a fake application
  `FAKE00000` with `sceSystemServiceLaunchApp` for the foreground user, attaches with ptrace,
  changes memory protection with kernel helpers and replaces that application's code with the
  homebrew ELF. The homebrew therefore runs **as the foreground application**.
- **How AKENO runs.** ps5-payload-dev/elfldr spawns payloads with `rfork` + `execve` of the
  SceSpZeroConf system daemon (`/system/vsh/app/NPXS40112/eboot.bin`, `elfldr.c`). AKENO is a
  background system process, not an application; the web browser (or a game) owns the focus.
- **The firmware has a focus model for audio.** `libSceAudioOut` exports (names in the SDK stub,
  generated from firmware exports) include `sceAudioOutGetFocusEnablePid`,
  `sceAudioOutAttachToApplicationByPid`, `sceAudioOutDetachFromApplicationByPid` and
  `sceAudioOutChangeAppModuleState`. Their existence indicates that audio output is enabled per
  application process ("focus-enabled pid"). None has a public declaration.

**Hypothesis (consistent with the v1 observations, not proven):** a port opened by a background
payload is accepted and consumed but mixed silently, because the payload's process is not the
focus-enabled application. This also explains why games are audible.

What would *not* be acceptable as a "fix", and is not done: running AKENO as a fake application
through process replacement (it kills the running game, uses ptrace and kernel memory helpers and
writes to `/system_ex`), calling the attach/focus functions without declarations, or patching the
audio service.

### What audio lab v2 measures to confirm or refute it

1. **Timing of every output call** (level 3). If calls 2…n block for about one block's duration
   (1024 frames = 21.3 ms), the audio subsystem consumed the samples at playback speed. If they
   return immediately, the samples were discarded without being played.
2. **Delayed test.** Start the tone with a 10 s delay, press the PS button and wait on the home
   screen: the browser is then no longer in front. Heard there but not with the browser in front
   would show that focus decides.
3. **Browser audio test.** The page's own Web Audio tone, played by the system browser, which *is*
   the foreground application. If the browser is audible, the realistic path for PS5 voice is the
   page (browser) and not the payload.
4. **Formats and block sizes** of the SDL driver, to exclude a format problem.
5. **Your report**, per test, of what you heard and where. Nothing is called "audible" otherwise.

## Audio lab v2

A separate ELF: `akeno-network-ps5-audiolab-v2.elf`. It is the complete AKENO app (profiles, quick
access, messages, game activity) plus Diagnostics → **Audio lab v2**.

| Feature | Details |
| --- | --- |
| States | Not supported · Failed to initialize · Failed to open · Failed to submit samples · Samples accepted (audible output NOT confirmed) · Samples accepted, but nothing was heard · **Audible output confirmed by you** (only after your report) |
| Diagnostics per test | firmware, library and exported functions, `sceAudioOutInit` result, user ID, port type, rate, channels, format, frames per call, `sceAudioOutOpen` result and port handle, port state (not readable: no verified declaration), output results (calls ok / first error / last result), samples submitted, duration, nonzero PCM and peak, per-call timing (min/avg/max) and "paced" verdict, buffer alignment, close result, errors, routing |
| Presets (all inside the SDL driver's configuration) | S16 stereo 1024 (default) · S16 mono 256 (v1, for comparison) · float stereo 1024 · S16 stereo 2048 |
| Levels | Quiet −26 dBFS · **Moderate −20 dBFS (default)** · Clearer −14 dBFS; hard limit −6 dBFS in code |
| Tone | 440 Hz, 1.5 s, 10 ms fades, 250 ms silence before and after |
| Safety | Nothing plays at start-up or on its own; a confirmation is required; one test at a time; 3 s pause between tests; at most 30 tests per launch; a waiting or playing test can be cancelled; the port is always closed |
| Browser comparison | read-only feature check (Web Audio, audio element, getUserMedia/WebRTC present — never called) and an optional Web Audio test tone with its own report |
| Report | Diagnostics → *Show report* (plain text, screenshot it) and `/data/akeno/audiolab-report.txt`; no tokens, credentials or account data |
| Probe (read-only) | library files in `/system/common/lib`; the four used functions; **looked up only**: `sceAudioOutGetPortState`, `SetVolume`, `GetLastOutputTime`, `GetHandleStatusInfo`, `GetSystemState`, `SetMainOutput`, `GetFocusEnablePid`, `AttachToApplicationByPid`, `DetachFromApplicationByPid`, `ChangeAppModuleState`; `libSceAudioIn` names (never called) |

### Output routing

The main port of the system user is routed by the system (TV/HDMI, or a headset when the console's
settings send all audio there). No verified interface lists or selects output devices, so none is
offered. Your report records where you heard the tone: TV/HDMI, headset on the controller, USB
headset or elsewhere.

## Microphone research (read-only)

| Question | Finding |
| --- | --- |
| PS5 SDK support | **None.** The pinned ps5-payload-sdk has no `libSceAudioIn` stub, header or sample. |
| Credible PS5 implementations | Until 2026-10-14 none found (ps5-payload-dev/SDL is output-only; websrv, elfldr and the pacbrew ports contain no capture code). **Update 2026-10-17:** EVO Player / PS5-Audio-Passthrough-Research's Speaker Lab capture from the DualSense mic in a **foreground app** — see [Microphone and voice interfaces seen elsewhere](#microphone-and-voice-interfaces-seen-elsewhere-not-used). |
| Exported names on 12.20 | `sceAudioInOpen`, `sceAudioInInput`, `sceAudioInClose` exist (your v1 probe). |
| PS4 declarations | PS4 homebrew headers declare a capture API with these names. The 2026-10-17 sources use the **same argument list on the PS5** (foreground app); for a background payload it is still unverified. |
| User ID / permissions | Unknown. On the PS4, capture ports are opened for a logged-in user, not the system user; whether a background payload may open one at all is unknown. |
| Controller / headset microphone ownership | The system and the running game own them (party chat, games). Taking them over is out of bounds. |
| Focus | If output is focus-gated (above), capture very likely is too. |

**Not done:** no AudioIn function is called, no audio is recorded, stored or transmitted, and no
build contains capture code (CI fails if the platform layer mentions `sceAudioIn`).

**Evidence required before a capture prototype:**
1. A credible PS5 source (or documentation) declaring `sceAudioInOpen`/`Input`/`Close` with
   argument types, and showing a successful capture from homebrew.
2. Confirmed audible *output* first (this lab), from the same process context capture would use.
3. Your separate approval of a hardware test with a printed checklist: microphone permission
   expectations, a visible recording indicator, local-only level meter, nothing stored or sent.

## Reading the audio lab v2 report

Each run in `/data/akeno/audiolab-report.txt` has the lines *Request*, *Library / sceAudioOutInit /
sceAudioOutOpen*, *sceAudioOutOutput*, *Submitted*, *Timing*, *sceAudioOutClose*, *Outcome* and
*User report*. They separate the possible causes:

| Report shows | Cause class | Meaning |
| --- | --- | --- |
| `sceAudioOutInit` or `sceAudioOutOpen` returns `0x8026xxxx` for **some** presets only | unsupported format / parameters | that format, rate or block size is refused on 12.20 |
| `sceAudioOutOpen` fails for **every** preset | API or permission error | the process may not open an output port at all; the code identifies it |
| open succeeds, `sceAudioOutOutput` errors (first error / call number) | submission error | the port refuses data; the code identifies it |
| all calls succeed, *Timing*: "returned faster than real time" | samples discarded | the system accepts and drops the blocks — no device consumes them |
| all calls succeed, *Timing*: "consumed at playback speed", *Submitted* PCM nonzero, not heard | accepted, paced, silent | a device clock consumes the port, but its mix does not reach the TV/headset: routing or mixing of a background process (focus, mute, attachment) — **this is the case the next diagnostic targets** |

"Paced" is a strong but indirect signal: it shows that an audio clock blocked the caller, not
which device it fed. The PS5-Audio-Passthrough-Research (below) shows the same blocking behaviour
for working ports (`sceAudioOutOutput` "blocks for each grain, which paces the thread").

## New external evidence (reviewed 2026-10-17)

Sources reviewed as **references only** (no code copied; see [LICENSES.md](LICENSES.md)):
[boykopovar/AnyPS5](https://github.com/boykopovar/AnyPS5) at `f517240` (GPL-2.0-or-later) and
[sainsaji/PS5-Audio-Passthrough-Research](https://github.com/sainsaji/PS5-Audio-Passthrough-Research)
at `c3be9c2` (GPL-3.0). AnyPS5 **reimplements** PS5 libraries on a PC: its behaviour is inferred
from games' calls and other emulators unless its notes name a console measurement. The passthrough
research is **hardware-tested on a PS5 Pro, firmware 12.70**, from an installed sandboxed
**foreground** app — not a background payload and not 12.20.

| Topic | Finding | Source and reliability |
| --- | --- | --- |
| Normal PCM output | `sceAudioOutInit()`, `sceAudioOutOpen(0xFF, 0, 0, 256, 48000, 1)` (system user, main port, S16 stereo), a blocking `sceAudioOutOutput` per 256-frame grain, `sceAudioOutOutput(h, NULL)` to drain, `sceAudioOutClose` — **audible** | passthrough sample app (PPSA99051), **hardware, 12.70, foreground app**. Same parameters as AKENO's v1/v2 presets, so the parameters alone are not the likely cause |
| `sceAudioOutInit` already initialised | `0x8026000E` is treated as success | passthrough sample app (hardware) |
| Error codes | `0x80260003` unknown port handle, `0x80260004` null pointer, and for `sceAudioOutOpen` `0x8026000A` bad port type, `0x80260006` zero length, `0x80260008` zero rate (AnyPS5, PS4-derived); `0x80260015` invalid Ex mode, `0x8026000C` bad HDMI type, `0x80268009` bad control (passthrough, from 12.70 disassembly and console return codes) | mixed: emulator / hardware |
| Routing evidence on the console | the **kernel log** (klogsrv, TCP 3232) prints `[AvControl] audio: port:HDMI src:… fmt:…` and `AudioOut: exclusive (pid=…)` when HDMI output is reconfigured | hardware, 12.70. Whether a plain PCM port logs anything is not shown |
| Exclusive HDMI | after `sceAudioOutExConfigureOutput` "all PCM ports of the process are muted (the process holds HDMI audio exclusively)"; the Settings menu only uses re-encode modes | hardware, 12.70. **Not used by AKENO** (changes HDMI output mode; needs separate approval) |
| Symbol lookup | "`sceKernelDlsym` is refused in a sandboxed app"; linked imports work | hardware, 12.70, sandboxed app. AKENO is not sandboxed (elfldr) and its `dlsym` lookups work on 12.20 (v1/v2 probe) |
| `AudioOutPortState` | `uint16 output; uint8 channel; uint8 reserved; int16 volume; uint16 rerouteCounter; uint64 flag; uint64 activeState` (24 bytes); `output` 1 = main/HDMI, 0x40 = personal/voice, 4 = pad speaker, 0x80 = aux | **emulator model** (AnyPS5 `SceTypes.hpp`, `sceAudioOutGetPortState`); PS4-era headers describe a 32-byte layout with the same first fields. **No console measurement** — its size must be measured, not assumed |
| `sceAudioOutGetLastOutputTime(handle, uint64*)` | in the emulators: the process time at which the last block was *accepted*, "not the time the block reached the device" | AnyPS5 notes: signature from PS4 reimplementations, "not confirmed on a PS5 title" |
| Audio timing vs. playback | emulator pacing is wall-clock based (SDL), so it says nothing about the console's device clock | AnyPS5 (emulator) |
| Port types | main 0, BGM 1, voice 2, personal 3, pad speaker 4, Ex 6, vibration 10, Audio3D 126, aux 127 | AnyPS5 (PS4 numbering); only **main 0** is hardware-verified on the PS5 |
| Process ownership / focus | no source models or measures audio focus for non-foreground processes; AnyPS5 ignores the user ID; `sceAudioOutGetFocusEnablePid` / `AttachToApplicationByPid` remain **names only** | none — the gap this lab measures |
| Controller in a payload | a ps5-payload-sdk payload on **13.60** got `scePadOpen` = `0x809b0081` (same as AKENO on 12.20) | AnyPS5 issue #3177, hardware. Suggests background payloads are refused **controller** access; whether the same applies to **audio** is not shown |

**Foreground homebrew vs. background payload.** Every hardware-verified audible PS5 homebrew
output found so far runs as a foreground application (SDL homebrew launched as `FAKE00000`; the
passthrough sample app installed as `PPSA99051`; EVO Player). No source shows audible output from a
background payload such as AKENO (SceSpZeroConf daemon under elfldr). Together with the controller
result this makes the process type the most plausible difference — still **unconfirmed** for audio.

## Microphone and voice interfaces seen elsewhere (not used)

| Interface | Declaration | Reliability | Relevance |
| --- | --- | --- | --- |
| `sceAudioInOpen(int user, int type, int index, uint32 grain, uint32 freq, uint32 format)`, `sceAudioInInput(handle, void*)`, `sceAudioInGetSilentState(handle)`, `sceAudioInClose(handle)` | DualSense mic opened for the initial user, type 1 ("general"), 256 frames, 16 kHz, format 0 (S16 mono) | passthrough sample app: "signatures from EVO Player, DualSense capture validated there" — **foreground app, ~12.70, the authors' claim**; AnyPS5 and shadPS4/SharpProspero/KytyPS5 agree on the arguments (emulators) | first credible PS5 declaration; **does not** show capture from a background payload |
| `sceAudioInHqOpen`, `sceAudioInAsyncOpen` (same arguments), `sceAudioInIsPadEmbeddedMic(handle)` | — | emulator-inferred | none yet |
| libSceVoice (`sceVoiceInit/Start/CreatePort/ConnectIPortToOPort/ReadFromOPort/WriteToIPort/…`) | PS3/Vita-style port graph | AnyPS5 stubs; signatures assumed from Vita/PS3 | a possible codec path; unverified on the PS5 |
| `sceAudioOut2EnableChat` / `DisableChat` | no arguments ("allows chat audio alongside the application's own") | SharpProspero via AnyPS5, unverified | suggests the system arbitrates chat audio per application |
| libSceVoiceChat, libSceProprietaryVoiceChatHelper | PSN system voice-chat groups; titles announce their own voice chat to the system | AnyPS5 stubs | system party chat, not an interface for AKENO |

The capture evidence requirement below is therefore **partly** met (item 1: a credible declaration
and a capture from homebrew — in a foreground app). Items 2 (audible output from the same process
context) and 3 (a separately approved test) are not. **No AudioIn call is made.**

## Next step toward PS5 voice parties, decided by the test C results

PS5 voice chat is **not working and not claimed**. Each outcome of test C (hardware test plan)
points to one next step; nothing beyond it is started before the result exists.

**12.20 result so far:** C3 not heard, C5 not heard, C6 *Web Audio API missing* — so the first row
("voice through the page with Web Audio") is ruled out, and the remaining rows depend on the C4
timing line of the report. With "paced", the next step is the read-only port-state diagnostic
proposed below; with "not paced", native output from a background payload is unavailable on 12.20.

| C4 timing | C3 heard (browser in front) | C5 heard (home screen) | C6 browser tone heard | Meaning | Next evidence-based step |
| --- | --- | --- | --- | --- | --- |
| paced | no | no | **yes** | The payload's audio is consumed but muted; the browser (app in front) can play | **Voice through the page**: check what the PS5 browser offers for capture and real-time media (`getUserMedia`, `RTCPeerConnection` presence is already reported read-only); if capture is missing, a payload→page audio path (payload receives voice over the existing WSS, page plays it with Web Audio) gives *listen-only* party audio first |
| paced | no | **yes** | any | Output depends on which app is in front | Measure when AKENO's process is allowed audio (home screen vs. browser vs. game) — still output-only; no capture |
| paced | **yes** | any | any | Native output works from the payload | Prototype D, output half: play a party's mixed audio received over WSS from a server-side bridge (listen-only), then the capture research below |
| not paced | no | no | any | Samples are discarded without playback | Native output from a background payload is not available; only the browser route remains |
| any failure state | – | – | any | A call failed (code in the report) | Compare the code with the SDL driver's behaviour; adjust within the verified parameters only |

Microphone input stays blocked on missing evidence regardless of these results: a credible PS5
declaration of `sceAudioIn*` (or a working PS5 homebrew that captures) and a separately approved,
single test. The branch `feature/akeno-ps5-direct-capture` (another session's read-only capture
diagnostic) is not part of Alpha 0.5; its findings should be read before any capture prototype.

## Proposed next diagnostic: audio lab v3 — one read-only port-state read

**Not implemented yet; for approval.** One addition to the existing v2 tone test, nothing else
changes (same presets, levels, confirmation, limits, report):

- After `sceAudioOutOpen` succeeds, AKENO calls **`sceAudioOutGetPortState(handle, buffer)`** on
  **its own port** three times: before the first block, in the middle of the tone, and before
  `sceAudioOutClose`.
- `buffer` is a 64-byte-aligned, 256-byte buffer pre-filled with a guard pattern. The report shows
  the return code, **how many bytes the call wrote** (which measures the real structure size
  instead of assuming 24 or 32 bytes) and the raw bytes in hex. A decoded line (`output`, `channel`,
  `volume`, `rerouteCounter`, `flag`) is labelled "decoded with the PS4/emulator layout, unverified".
- Nothing is set: no volume, routing, focus, attach/detach, HDMI mode or privilege call. The only
  new import is that one function, resolved with `dlopen`/`dlsym` like the others; the release ELF
  is unchanged (CI checks).

What it answers:

| Port state on 12.20 | Interpretation |
| --- | --- |
| call fails (code) | the function is not usable from the payload; nothing else is learned |
| `output` = 0 (no device bit) while the calls are paced | the port is open and clocked but **connected to no output** — routing of the background process |
| `output` has the main/HDMI bit, `volume` > 0, still silent | the port claims HDMI: the loss is later in the mix (focus/mute of the process) |
| `volume` = 0 | the port is muted at the port level |

Optional, no AKENO change: if a kernel-log server already answers on the console (TCP 3232),
capture its lines during one tone test; `[AvControl]`/`AudioOut` lines naming AKENO's process would
add the system's own view. Installing a log server is the owner's decision.

## Hardware test plan

See [HARDWARE_TEST_PLAN_PS5_ALPHA_0.5.md](HARDWARE_TEST_PLAN_PS5_ALPHA_0.5.md), test C. Run it from
the home screen; **do not** run audio tests while a game runs and do not attempt microphone capture
(both need a separate approval).

## Path to a PS5 party participant (unchanged)

If the browser plays audio but the payload cannot, voice on the PS5 has to go through the browser
page, which would also need microphone access in the PS5 browser (not available as far as known).
Options and their trade-offs are unchanged from Alpha 0.4: a native WebRTC client in the payload
(needs working payload audio) or a server-side bridge (needs working capture). Neither is
started; both need levels 3–4 above first.

## Code

`clients/ps5/app/audio_lab.c` / `.h` (tests, states, report), `audio_lab_pcm.c` / `.h` (tone
generation, unit-tested), the `AKENO_EXPERIMENTAL_AUDIO` sections of `platform_ps5.c` (the only
place that calls `sceAudioOut*`, resolved with `dlopen` at the first test), `platform_host.c`
(simulation with failure injection: `AKENO_SIM_AUDIO`, `AKENO_SIM_AUDIO_FAIL=init|open|output|close`,
`AKENO_SIM_AUDIO_PACE=none`), `app_routes.c` (`/api/diagnostics/audio/*`) and `web/audiolab.js`
(served only by the lab build; the release serves an empty script). The release ELF contains none
of this, which CI checks (`console/check-imports.sh`, symbol and string checks).
