# Direct PS5 gameplay capture — research

_Research date: 2026-10-16. Console target: PS5, firmware 12.20, kstuff jailbreak, custom DNS.
Pinned SDK: ps5-payload-dev `f5d40f02770e9b0001dd9e18056eca70b3da8b40` (the SDK the AKENO console
app builds with)._

Goal: obtain the **real PS5 gameplay picture and game audio on the console itself** and feed it
into the existing AKENO SharePlay transport (LiveKit), with no capture card and no PC running
Remote Play. This document is the evidence base; the capability matrix is in
[PS5_CAPTURE_CAPABILITIES.md](PS5_CAPTURE_CAPABILITIES.md), the prioritised plan in
[PS5_DIRECT_SHAREPLAY_PLAN.md](PS5_DIRECT_SHAREPLAY_PLAN.md).

Rules honoured throughout (also the console's safety rules, `docs/SAFETY.md`): no undocumented
function signatures, no kernel patches/offsets, no cross-process memory reads or code injection,
no credential capture, no DNS/account/firmware changes, no contacting PSN or Twitch, no claim
without evidence. The investigation method the user approved is a **read-only diagnostic ELF**
(see [PS5_CAPTURE_DIAGNOSTIC_TEST.md](PS5_CAPTURE_DIAGNOSTIC_TEST.md)).

Evidence classification used below: **source-verified** (name in a verified source) ·
**runtime-detected** (file found on the console) · **signature-verified** (symbol resolved on
the console) · **hardware-tested** (a confirmed on-console result) · **unknown** · **unsupported**.

## 0. What changed since the previous research

[SHAREPLAY_PS5_RESEARCH.md](SHAREPLAY_PS5_RESEARCH.md) concluded native capture was UNSUPPORTED
and reused a capture card / Remote Play window. That conclusion **still holds for direct
framebuffer access** (track A). What this round adds:

- A precise, re-inspected map of the pinned SDK's capture-relevant exports (track A), confirming
  there is *no* screenshot/record/broadcast/encoder stub and that the `sceShellCoreUtil*`
  GameLiveStreaming/SharePlay symbols are **status/budget orchestration only**.
- A serious evaluation of the **built-in broadcast pipeline** (track B) as the one *native,
  hardware-encoded* no-capture-card path — with its real blockers (account/PSN, DNS takeover
  conflicting with the jailbreak DNS, latency, firmware).
- A read-only **capability diagnostic ELF** so the remaining unknowns are answered with hardware
  evidence instead of assumption. This is the correction to the earlier research being "too
  restrictive": rather than stop at "no SDK header", we now *measure* file/symbol presence on
  the console — while still never calling the functions.

Where the earlier research remains correct: direct cross-process framebuffer capture is not
available to our payload, and a screenshot/clip path is not live SharePlay.

## 1. Track A — internal capture services in the pinned SDK (source-verified)

Method: inspected every stub in `sce_stubs/` and the `ps5/*.h` headers of the pinned SDK.

| Capability | Finding in the pinned SDK | Class |
| --- | --- | --- |
| Screenshot library (`libSceScreenShot`) | **No stub present** | not in SDK |
| Video recording (`libSceVideoRecording`) | **No stub present** | not in SDK |
| Broadcast client (`libSceGameLiveStreaming`) | **No stub present** | not in SDK |
| Hardware video encoder | **No stub present**, no encoder header | not in SDK |
| `libSceVideoOut` | Present. Exports drive the **caller's own** display output; the only read-back is `sceVideoOutSysReadCrc` / `…ReadCrc64` — a **checksum of the frame, not the pixels** | source-verified |
| `libSceSystemService` | Present. Exports `sceSystemServiceIsGameLiveStreamingOnAir` and `sceShellCoreUtil{Get,Set}GameLiveStreamingStatus`, `…PreNotifyOfGameLiveStreaming`, `…SetGameLiveStreamingOnAirFlag`, `…{Get,Set}SharePlayStatus`, `…PreNotifyOfSharePlay`, `…{Acquire,Release}SharePlayCpuBudget`, `…ActivateRecordActivation` — **status, pre-notify and CPU-budget hooks only; no pixel or encoder access** | source-verified |
| `libSceRemoteplay` | Present. Exports control/query the **system Remote Play server** (`sceRemoteplayInitialize`, `…GetConnectionStatus`, `…Prohibit[Streaming]`, `…Approve`, pin codes). It governs the server; it does **not** hand the encoded video to our process | source-verified |
| `libScePad`, `libSceVideoOut`, `libSceAudioOut` | Present (used elsewhere in AKENO) | source-verified |

**Conclusion (track A).** The pinned SDK exposes no interface that returns the pixels or an
encoded stream of a *running game* to our process. `ReadCrc` is a checksum; `VideoOut` is our own
output; the ShellCoreUtil hooks are orchestration. Reading another process's framebuffer would
require cross-process memory access, which AKENO does not do. Direct framebuffer/encoder capture
is therefore **unsupported today**. Whether the *files* `libSceScreenShot.sprx`,
`libSceVideoRecording.sprx`, `libSceGameLiveStreaming.sprx` exist on 12.20 and export the PS4-era
names is **unknown until measured** — exactly what the diagnostic's RESOLVE tier records (names
sourced from the OpenOrbis PS4 SDK and flagged as PS4-origin, never called).

## 2. Track B — the built-in broadcast pipeline (the native, no-capture-card path)

The PS5 can broadcast gameplay to Twitch/YouTube using its **own hardware encoder** — no capture
card, no PC encode. Two public projects redirect that stream to a local receiver:

- [EnixCoda/PS5-Streamer](https://github.com/EnixCoda/PS5-Streamer) — a `dnsmasq` container that
  redirects the console's streaming DNS to a local `nginx-rtmp` which receives the RTMP feed.
- [ImLunaHey/playstation-rtmp](https://github.com/ImLunaHey/playstation-rtmp) — "capture PS5
  gameplay without a capture card — DNS-spoofs Twitch and reroutes the RTMP stream to your PC."
- Write-ups: Adafruit "Hijacking the PS5's RTMP stream" (2026-09-29) and yashgarg.dev.

How it works, and the blockers (treat project claims as **unverified on 12.20**):

| Question (docs §8) | Evidence | Class |
| --- | --- | --- |
| 1. Works without Sony authentication? | It is the console's **native Twitch broadcaster**, reached through the Create menu which is behind the user's account/sign-in; the write-ups assume an account is linked. No evidence it runs signed-out. | unknown → likely **needs account/PSN session** |
| 2. Needs a previously linked streaming account? | Yes — a **linked Twitch account** is required; the redirect only changes *where the already-authorised stream goes*. | source (project) |
| 3. Can the destination be chosen by supported config? | No supported setting; it is changed only by **DNS spoofing** of the ingest hostname. | source (project) |
| 4. Is DNS redirection required? | **Yes.** The console resolves `contribute.live-video.net` each time; whoever answers DNS controls the destination. | source (project) |
| 5. Conflict with the jailbreak DNS (`62.210.38.117`)? | **Yes, direct conflict.** The console has one primary DNS. Pointing it at a local RTMP receiver replaces the jailbreak resolver; doing both needs the receiver to also *forward* every other query to `62.210.38.117`. AKENO must not change console DNS. | analysis |
| 6. Encrypted RTMPS blocks transparent redirect? | The main `ingest.twitch.tv` uses **RTMPS with certificate validation** → a self-signed local cert fails on the PS5. The projects spoof `contribute.live-video.net` which the console contacts over **plain RTMP (1935)**. | source (project) |
| 7. Available while a game runs? | The native broadcaster is designed to run during gameplay; **unverified on 12.20**. | unknown |
| 8. Game audio included? | The native broadcast includes game audio; **unverified here**. | unknown (likely yes) |
| 9. Expected latency? | RTMP broadcast latency is **several seconds** (community reports). Fine for watch-only; **unsuitable for interactive co-op / Player 2**. | source (community) |
| 10. Can the receiver feed LiveKit? | Yes in principle: a local RTMP→WebRTC bridge (e.g. ingest to a LiveKit track) could feed the existing SharePlay room. This is new receiver infrastructure, not a console change. | analysis |

Extra facts: native broadcast caps **1080p60**; the **YouTube** path dies after ~60 s because the
PS5 polls YouTube's API to confirm the stream is live (so a blind redirect is Twitch-only).

**Conclusion (track B).** This is the most promising *native, no-capture-card* path, but only for
**watch-only** SharePlay, and only if: a Twitch account is linked, the console can reach the
broadcast on 12.20, and the user accepts a **DNS arrangement that must not clobber the jailbreak
DNS**. It cannot serve Player 2 co-op (latency). AKENO must **not** auto-deploy DNS redirection;
it stays a user-set-up, separately-approved prototype (plan track B). Whether 12.20 can even
start the broadcast signed-in-as-configured is the key unknown the hardware test must answer.

## 3. Track C — other console-native capture paths

Distinguish (they are **not** equivalent; none is live SharePlay today):

- **True live capture** — continuous frames of the running game to our process: **no interface**
  (track A). Unsupported.
- **Screenshot** (`libSceScreenShot`, PS4-origin names): at most a still on request, historically
  of the *caller's* surface. A slideshow is not SharePlay. File/symbol presence = diagnostic.
- **Recorded-clip access** (`libSceVideoRecording`): reads a clip written to storage — not live.
  (The AKENO-unrelated apps "PS5 Captures"/"PShare" only move *already-saved* clips.)
- **Native broadcast reuse**: track B.

No credible PS5 homebrew that delivers *live game frames to another process* was found. (A
project advertising "fast screen capture" for an AI-agent loop exists but its method is
undocumented and may be own-process or capture-card-assisted; unverified.)

## 4. Track D — offline activation / "fake PSN" (relevance check)

[bucanero/apollo-ps4](https://github.com/bucanero/apollo-ps4) and similar do **local account
activation**: a locally assigned account ID that enables offline features and save-data handling.
This is **not** genuine PSN authentication and grants **no** Sony online access.

Relevance to capture: **essentially none.** Screenshot/record/encoder access is a *process-context
and interface* question, not an account question. The one tangential link is track B: the native
broadcaster is gated behind the account/Twitch-link UI, and **offline/fake activation does not
satisfy a real Twitch/Sony broadcast session.** AKENO keeps its own party account system,
independent of PSN; we do not spoof Sony login or build a fake Sony service. **Prioritise other
work over this track.**

## 5. Track E — official Share Play vs AKENO SharePlay (architecture)

Official PS5 Share Play separates: console-side capture, hardware encode, signalling, participant
**authentication (PSN)**, NAT traversal, transmission, and remote controller input. The pixels do
not necessarily traverse Sony servers, but **session setup and authorisation are PSN-bound**, and
there is no supported interface for a third-party payload to join or reuse that session.

AKENO therefore does **not** impersonate or reuse Sony's Share Play session. The existing backend
already provides the non-capture pieces with AKENO's own party membership and authorisation:
party creation/invites, voice, SharePlay authorisation, LiveKit video rooms, the controller
transport, and Player 2 grants/revocation ([SHAREPLAY_ARCHITECTURE.md](SHAREPLAY_ARCHITECTURE.md)).
**The only missing piece is obtaining the gameplay stream on the console** — tracks A–C.

## 6. Overall conclusion

1. **Direct native capture (framebuffer/encoder): not implementable now** — no verified interface,
   and the methods that might exist are off-limits by our rules (track A).
2. **Built-in broadcast reuse: the most promising native path, watch-only**, gated on account/PSN,
   a DNS arrangement that must not disturb the jailbreak DNS, multi-second latency, and
   12.20-unverified behaviour (track B). Candidate for a separately-approved prototype, not now.
3. **Offline activation: irrelevant** to capture (track D).
4. The right deliverable today is the **read-only capability diagnostic ELF**, which turns the
   remaining unknowns (do the capture libraries exist on 12.20? do the PS4-era symbols resolve?
   is the broadcast status queryable?) into hardware evidence — without calling anything.

## Hardware run 1 and the diagnostic v2 (2026-10-16)

The first diagnostic build was run on a real PS5 (firmware 12.20, build `35189361f6c1-ps5-capturediag`).
**STATIC worked** and confirmed all six candidate library files exist (incl. `libSceScreenShot.sprx`,
`libSceVideoRecording.sprx`, `libSceGameLiveStreaming.sprx`). Pressing the single all-libraries
**RESOLVE** button made the app exit ("AKENO NETWORK is not running"), with no RESOLVE line in
`akeno.log` — the build logged only after the whole batch, so the failing operation was not captured.

Likely cause (**hypothesis, not confirmed** — awaiting a v2 log from the console): on the pinned
SDK, `dlopen` of an unmapped module calls `sceKernelLoadStartModule` (loads *and starts* it), and
the old `dlclose` called `sceKernelStopUnloadModule` (stops *and unloads* it). Starting, or more
likely unloading, an unverified system capture/broadcast module can terminate the process. Diagnostic
v2 removes the risk while keeping the measurement: one library per explicit action, a flushed
`akeno.log` line before/after every `dlopen` and `dlsym`, and **no `dlclose`** (the module is left
loaded for the diagnostic's short life). The next hardware run's `akeno.log` should name the exact
operation and library; only then is the cause confirmed.

Sources: pinned ps5-payload-sdk `sce_stubs/*`, `include/ps5/*` and `crt/rtld_*.c` (read locally);
[EnixCoda/PS5-Streamer](https://github.com/EnixCoda/PS5-Streamer);
[ImLunaHey/playstation-rtmp](https://github.com/ImLunaHey/playstation-rtmp);
Adafruit "Hijacking the PS5's RTMP stream" (2026-09-29) and the HN discussion;
[bucanero/apollo-ps4](https://github.com/bucanero/apollo-ps4); OpenOrbis PS4 SDK (symbol-name
origin for the non-SDK candidates). Project and community claims are unverified on firmware 12.20.
