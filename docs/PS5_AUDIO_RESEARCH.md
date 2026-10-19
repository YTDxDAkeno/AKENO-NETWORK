# PS5 audio: research, the silent tone, and audio labs v2 and v3

> **Update 2026-10-19 — the background voice service.** Voice Alpha 0.1.0 (a background payload)
> opened AKENO Native's audible output call on FW 12.20 and was not heard, on the home screen and in
> a game; its microphone call (Native 1.1's, which opened in the foreground) returned `0x809B0081`.
> Its "NOT paced" was an artifact of its own sleeping, not a discarded port (reproduced in
> simulation). With audio labs v1/v2 this makes three background payloads that were accepted and
> silent, against one foreground app that was heard with the same call: the process context
> decides. Details and the 0.2.0 test: [PS5_VOICE_AUDIO_0.2.md](PS5_VOICE_AUDIO_0.2.md).

> **Update 2026-10-17 — the foreground app is audible.** AKENO NETWORK Native 0.1
> (`d094e62bcaf1-ps5`) ran as the foreground application on the same PS5 with 12.20, played four
> `sceAudioOut` tone tests, and the owner heard all four
> ([native results](PS5_NATIVE_HARDWARE_RESULTS_FW12.20.md)). So AKENO's audio is audible from the
> **foreground** process. The background daemon's silence described below is still what that
> process gets. Voice chat is not claimed.

Status 2026-10-17 (background daemon). **Audible output from the AKENO daemon is NOT confirmed**: audio lab v2 was tested
on firmware 12.20; the system accepted and paced every block and none of its tones was heard
(below). Why is **not** established; the audio-focus idea below remains a hypothesis. Audio lab v3
(this commit, not yet run on a PS5) adds one read-only measurement of AKENO's own port and an HTML
`<audio>` element test. Microphone capture is not attempted. Desktop and phone voice (see [VOICE.md](VOICE.md)) do not depend on any of this.

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
[PS5_HARDWARE_RESULTS_FW12.20.md](PS5_HARDWARE_RESULTS_FW12.20.md). The owner read the complete
`/data/akeno/audiolab-report.txt` and confirmed these measured results for **every** completed
test with feedback (formats S16/float mono/stereo, 256–2048 frames per call, all three levels,
immediate and delayed):

| Report line | Measured on 12.20 |
| --- | --- |
| `sceAudioOutInit` | **0x00000000** (success) |
| `sceAudioOutOpen` (user 0xFF system, port type 0 main, 48000 Hz) | **succeeded** — a port handle was returned |
| `sceAudioOutOutput` | **every call succeeded** (no first error) |
| Submitted / PCM nonzero | **yes**: nonzero samples were submitted (tone between 250 ms of silence before and after) |
| Timing | **"consumed at playback speed"**: calls 2…n blocked for about one block's duration each |
| User report | **not heard**, for every test — with the browser in front and, in the delayed test, on the home screen |
| Browser comparison (C6) | "API missing, state unavailable, heard −1": the 12.20 browser has **neither `AudioContext` nor `webkitAudioContext`** |

The message that reported these results gave the confirmed outcome of each line, not the
per-run numbers (handle values, call counts, per-call milliseconds); those are not copied here
because they were not part of it. Every v3 run records them again in its report.

**Classification ([Reading the audio lab v2 report](#reading-the-audio-lab-v2-report)):**
"accepted, paced, silent". The API works, the format is accepted, and an audio clock consumes
the port at playback speed — but nothing reaches the TV or headset. Ruled out on 12.20: an API
error, an unsupported format or block size, a submission error and discarded (unpaced) samples.

What the results already exclude or weaken:

- **Not the browser being in front:** the delayed test on the home screen was silent too, so "the
  browser owns the audio focus" alone does not explain the silence.
- **Not a format problem:** every preset opened and was paced, and every level was silent.
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

> Status 2026-10-17: **not confirmed and not refuted.** The v2 hardware test was accepted and paced
> but silent in every context, including the home screen. That fits a routing/mixing loss for a
> background process, which focus is one form of; it does not show that focus is the mechanism.
> Audio lab v3 measures what the system reports about the port itself.

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

**Update 2026-10-18 (AKENO Native 1.1.0-beta.1, owner-approved):** AKENO Native's platform layer
opens the microphone with the declaration above (`sceAudioInOpen(user, 1, 0, 256, 16000, 0)`),
loaded with `dlopen` only after the user confirms party voice or the microphone test
([PS5_VOICE.md](PS5_VOICE.md)). Still nothing is recorded or stored; the daemon and the audio labs
contain no capture code. **Not yet run on a PS5.**

**Before 1.1 (still true for the daemon):** no AudioIn function is called, no audio is recorded,
stored or transmitted.

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
| `AudioOutPortState` | `uint16 output; uint8 channel; uint8 reserved; int16 volume; uint16 rerouteCounter; uint64 flag; uint64 activeState` (24 bytes); `output` 1 = main/HDMI, 0x40 = personal/voice, 4 = pad speaker, 0x80 = aux | **emulator model** (AnyPS5 `SceTypes.hpp`, unsourced). shadPS4, fpPS4 and SharpProspero use 32 bytes with the same first fields (see [validation](#validation-of-sceaudiooutgetportstate-before-any-console-call)). **No console measurement** |
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

**12.20 result:** C4 **paced**, C3 not heard, C5 not heard, C6 *Web Audio API missing*. The first
row ("voice through the page with Web Audio") is ruled out; the case is "paced, silent everywhere",
which no row covered. The next step is audio lab v3 (below): read-only port state, and the
browser's `<audio>` element as the remaining page route.

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

## Audio lab v3: read-only port state and the HTML `<audio>` element

A separate ELF, `akeno-network-ps5-audiolab-v3.elf`: audio lab v2 unchanged (same presets, levels,
confirmation, limits, delayed test, report), plus two additions. **Not run on a PS5 yet.**

### Validation of `sceAudioOutGetPortState` before any console call

| Question | Evidence | Strength |
| --- | --- | --- |
| Does the PS5 export it? | Listed in the pinned ps5-payload-sdk `libSceAudioOut` stub, which is generated from firmware exports; audio lab v2 looked it up on 12.20 with `dlsym`. v3 calls it only when `dlsym` returns it | name: strong |
| Arguments | `int32 sceAudioOutGetPortState(int32 handle, SceAudioOutPortState *state)` in OpenOrbis (`include/orbis/AudioOut.h`, `(int32_t, void *)`), shadPS4 (`audioout.h`), fpPS4 (`ps4_libsceaudioout.pas`), SharpProspero (PS5, `Interop/Audio/AudioOut.cs`) and AnyPS5 (PS5). Five sources, two of them PS5-specific, agree on two arguments | good, but no console measurement |
| Real PS5 code calls it | AnyPS5 PR #2130: Unity's main thread in a title (MiniGolf) calls it at start-up | indirect (emulator observation) |
| Structure layout | `uint16 output; uint8 channel; uint8 reserved; int16 volume; uint16 rerouteCounter; uint64 flag; uint64 reserved[2]` = **32 bytes** in shadPS4, fpPS4 and SharpProspero (`Size = 32`); AnyPS5's 24-byte variant came in a bulk "add libs function signatures" commit without a source | 32 bytes: consistent, unmeasured |
| `output` bits | 0x00 unknown, 0x01 primary, 0x02 secondary, 0x04 tertiary, 0x40 headphone, 0x80 external (fpPS4 and shadPS4 constants, PS4 SDK names) | PS4 only |
| Behaviour | a query: fills the caller's structure, returns 0 or a negative `0x8026xxxx` code (not initialised, invalid port, invalid pointer, not opened in the emulators). No source shows it changing state | consistent |

Conclusion: the **name** is verified on 12.20; the **argument list** is consistent across every
source; the **structure size is not verified**. That is enough for a read-only call on AKENO's own
open port with a buffer that tolerates any plausible size and detects anything larger. It is not
enough to trust decoded fields, so they are labelled unverified everywhere.

### What v3 does with it

- Three measurements per test on the test's own port: **before the first block**, **in the middle
  of the tone** (after the block that crosses half the planned frames) and **before
  `sceAudioOutClose`**.
- Each measurement is two calls. The structure pointer is a 256-byte window, 64-byte aligned,
  inside a 4096-byte static arena; the 64 bytes before and the 3776 bytes after the window are
  canaries (`0xC3`). The window is filled with `0xA5` for the first call and `0x5A` for the
  second, so a written byte is seen even when its value equals one fill. The second call is
  skipped when the first returns an error.
- If any canary changes, the function wrote outside its window: the measurement is marked, and
  **the function is never called again in that launch** (the arena absorbs up to 3.7 KB of
  overrun).
- Recorded per call: the **raw return code**, the **raw window bytes**, the canary state; per
  measurement: which bytes were written, the highest written offset, whether both calls agree.
  The written-byte count is a lower bound only: a field the function leaves untouched is
  invisible, so it does not prove the structure's size.
- Decoded (PS4 layout, **unverified**): `output` with its bit names, `channel`, `volume`,
  `rerouteCounter`, `flag` — only for bytes a call actually wrote.
- Port-state results never change the test's outcome (accepted / failed / not heard).

### Safety evidence for every new call

| Call | Where | Why it is safe enough to run |
| --- | --- | --- |
| `dlsym(libSceAudioOut, "sceAudioOutGetPortState")` | audio lab ELF only, on the first test | same lookup as v2 (already run on 12.20); a missing name only skips the measurement |
| `sceAudioOutGetPortState(own_handle, window)` | audio lab ELF only, ≤ 6 calls per test, only while the test's own port is open | a query on AKENO's own handle; consistent two-argument declaration in five sources; guarded window with canaries and a permanent stop on overrun; no volume, routing, focus, attach/detach, HDMI, mode or privilege function is called |
| HTML `<audio>` playback of a WAV generated by the page | audio lab page only, after a button press and a confirmation | a standard browser API in the system browser (no system call); plays the same 1.5 s, 440 Hz tone at the selected level (≤ −14 dBFS); lab-only `media-src blob: data:` in the page policy, the release policy is unchanged |
| Feature checks (`canPlayType`, `MediaSource.isTypeSupported`, presence of `getUserMedia`/`RTCPeerConnection`) | audio lab page only | presence and capability queries; nothing is played, opened, recorded or asked for |

Release ELF: unchanged. CI rebuilds the hardware-tested Alpha 0.5 RC (`7e28255`) and builds this
commit's release source with the RC's build ID; both must give the same file (SHA-256
`c92e039b…`). The lab ELF imports exactly the release's 96 symbols; the new function is resolved
at run time.

### How to read the v3 port state

| Port state on 12.20 (paced, silent) | Interpretation | Next step |
| --- | --- | --- |
| the call fails (code) | not usable from the payload; nothing learned about routing | rely on the `<audio>` result |
| `output` without a device bit (0), calls agree | the port is open and clocked but the system attaches it to **no output** | evidence for a process-level routing restriction; read-only research of how the system attaches output to a process |
| `output` primary/headphone, `volume` > 0, still silent | the system claims the port reaches a device; the loss is later (mix/mute of the process) | compare with the `<audio>` result; no change to focus or routing without approval |
| `volume` = 0 | the port is muted at port level | find out who sets it (read-only) |
| bytes written beyond 32, or canaries changed | the PS4 layout is wrong on the PS5 | record the raw bytes; no decoding is trusted |
| `rerouteCounter` changes during the tone | the system re-routed the port while it played | compare with what you did (PS button, home screen) |

### HTML `<audio>` element and browser compatibility

Known: the 12.20 system browser has no Web Audio API (measured). Not known: whether it plays
`<audio>` at all — no reliable public compatibility data for the PS5's (unofficial) browser was
found; a WebKit engine is reported by press coverage only. v3 therefore **measures** instead of
assuming:

- a read-only feature table: Web Audio and OfflineAudioContext presence, the `<audio>` element and
  `play()`, Blob URLs, `canPlayType` for WAV, MP3, AAC, Ogg Vorbis/Opus and WebM Opus, Media
  Source Extensions, and the **presence** of `getUserMedia` and `RTCPeerConnection` (never called);
  the browser's user agent;
- a user-started `<audio>` test: the same tone as a 48 kHz 16-bit WAV generated by the page, from
  a `blob:` URL (or a `data:` URL without Blob URLs), with the `play()` result, the media events
  (loadedmetadata, canplay, playing, ended), `MediaError` code, duration and played time, then
  your report (heard where / not heard). The WAV is generated in the page because the console
  app's small HTTP server does not answer byte-range requests, which WebKit media loading uses.

What it decides: if `<audio>` is **heard**, the page has a working output path on 12.20 (the basis
for listen-only party audio through the page, with audio coming over the existing connection);
if it plays to the end but is **not heard** either, output from both the payload and the page is
lost, which points to the console's audio routing for these contexts rather than to AKENO's code.
**No microphone, WebRTC or capture is used or approved.**

## Hardware test plan

See [HARDWARE_TEST_PLAN_PS5_ALPHA_0.5.md](HARDWARE_TEST_PLAN_PS5_ALPHA_0.5.md): test C (audio lab v2,
done on 12.20) and **test D (audio lab v3)**. Run audio tests from the home screen; **do not** run
them while a game runs and do not attempt microphone capture (both need a separate approval).

## Path to a PS5 party participant (unchanged)

If the browser plays audio but the payload cannot, voice on the PS5 has to go through the browser
page, which would also need microphone access in the PS5 browser (not available as far as known).
Options and their trade-offs are unchanged from Alpha 0.4: a native WebRTC client in the payload
(needs working payload audio) or a server-side bridge (needs working capture). Neither is
started; both need levels 3–4 above first.

## Code

`clients/ps5/app/audio_lab.c` / `.h` (tests, states, report), `audio_lab_pcm.c` / `.h` (tone
generation, unit-tested), `audio_lab_state.c` / `.h` (v3: guarded port-state measurement and the
unverified decoding, unit-tested in `tests/unit/test_audio_state.c`), the `AKENO_EXPERIMENTAL_AUDIO`
sections of `platform_ps5.c` (the only place that calls `sceAudioOut*`, resolved with `dlopen` at
the first test), `platform_host.c` (simulation with failure injection: `AKENO_SIM_AUDIO`,
`AKENO_SIM_AUDIO_FAIL=init|open|output|close`, `AKENO_SIM_AUDIO_PACE=none`,
`AKENO_SIM_AUDIO_PORTSTATE=ps4|unconnected|fail|missing|overrun`), `app_routes.c`
(`/api/diagnostics/audio/*`, including `/media` for the `<audio>` result), `ui_server.c` (the
lab-only `media-src blob: data:`) and `web/audiolab.js` (served only by the lab build; the release
serves an empty script). The release ELF contains none
of this, which CI checks (`console/check-imports.sh`, symbol and string checks).
