# PS5 audio: research, the silent v1 tone, and audio lab v2

Status 2026-10-14. **Audible output from AKENO on a PS5 is NOT confirmed.** Microphone capture is
not attempted. Desktop and phone voice (see [VOICE.md](VOICE.md)) do not depend on any of this.

## What the hardware test of audio lab v1 showed

Your test of `akeno-network-ps5-audiolab.elf` (CI run 38558019000, `ae94851`) on firmware 12.20:

| Level | Result | What it proves |
| --- | --- | --- |
| 1. API calls succeed | `libSceAudioOut.sprx` loaded; `sceAudioOutInit/Open/Output/Close` resolved; `libSceAudioIn.sprx` exports `sceAudioInOpen/Input/Close` | The names exist in 12.20. Nothing about arguments, privileges or behaviour. |
| 2. Samples accepted | "accepted (7 runs)": every `sceAudioOutOutput` returned success | The system took the buffers. |
| 3. Samples played by the audio subsystem | **unknown** — v1 did not time the calls | — |
| 4. Sound reaches TV/headset | **No** (nothing heard; games are audible) | Something between 2 and 4 drops the audio. |

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

### The strongest lead: application audio focus

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

**Conclusion (hypothesis, consistent with every observation):** a port opened by a background
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
| Credible PS5 implementations | **None found.** ps5-payload-dev/SDL is output-only (`OnlyHasDefaultOutputDevice`); websrv, elfldr and the pacbrew ports contain no capture code. |
| Exported names on 12.20 | `sceAudioInOpen`, `sceAudioInInput`, `sceAudioInClose` exist (your v1 probe). |
| PS4 declarations | PS4 homebrew headers declare a capture API with these names. **That is the PS4**; nothing shows the PS5 functions take the same arguments, and calling them would be guessing. |
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

## Next step toward PS5 voice parties, decided by the test C results

PS5 voice chat is **not working and not claimed**. Each outcome of test C (hardware test plan)
points to one next step; nothing beyond it is started before the result exists.

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
