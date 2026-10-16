# PS5 capture capability matrix

Companion to [PS5_DIRECT_CAPTURE_RESEARCH.md](PS5_DIRECT_CAPTURE_RESEARCH.md). This is the
compiled-in catalogue the diagnostic ELF reports (`clients/ps5/app/capture_diag.c`), with its
classification. **No value here is "hardware-tested" until a real console result is recorded** —
CI and host simulation never set that level.

Classification (rising evidence): **source-verified** (name from a verified source) →
**runtime-detected** (file found on the console, stat) → **signature-verified** (symbol resolved
via dlsym; signature still unverified) → **hardware-tested**. Plus **unknown** and **unsupported**.

"Role" says what the candidate *would* give if usable, so a resolved symbol is never mistaken for
working capture.

## Capability verdict (fixed, honest — not inferred from symbol presence)

| Capability | Verdict | Why |
| --- | --- | --- |
| Live framebuffer capture of a running game | **unsupported** | No verified interface; AKENO does not read another process's memory |
| Screenshot (still) | **unsupported for SharePlay** | At most a still of the caller's surface; a still is not live |
| Recorded-clip access | **unsupported for SharePlay** | Reading a saved clip is not a live stream |
| Native broadcast reuse | **not available as a drop-in** | Needs a linked account + network session; destination chosen by the system (DNS), multi-second latency — see research track B |
| Hardware video encoder (direct) | **unsupported** | No encoder interface in the pinned SDK; no verified library/signature |

## Candidate libraries (what the diagnostic probes)

| Library | Role | In pinned SDK | Symbol-name source | Baseline class | Gives live game frames? |
| --- | --- | --- | --- | --- | --- |
| `libSceVideoOut.sprx` | own-process output | yes | pinned SDK stub | source-verified | no (own output; `ReadCrc` = checksum) |
| `libSceSystemService.sprx` | status-only | yes | pinned SDK stub | source-verified | no (GameLiveStreaming/SharePlay status + CPU budget) |
| `libSceRemoteplay.sprx` | status-only | yes | pinned SDK stub | source-verified | no (controls the system Remote Play server) |
| `libSceScreenShot.sprx` | screenshot | no | OpenOrbis PS4 SDK (PS4 origin, unverified on PS5) | source-verified | no (single still) |
| `libSceVideoRecording.sprx` | recorded-clip | no | OpenOrbis PS4 SDK (PS4 origin, unverified on PS5) | source-verified | no (clip to storage) |
| `libSceGameLiveStreaming.sprx` | native-broadcast | no | OpenOrbis PS4 SDK (PS4 origin, unverified on PS5) | source-verified | no (account/network-gated broadcaster) |

The diagnostic raises a candidate to **runtime-detected** when its file exists on the console, and
to **signature-verified** when a listed symbol resolves (RESOLVE tier, user-initiated). It never
calls any of them, and never raises the capability verdict above regardless of what resolves.

## Non-capture interfaces, for completeness

| Interface | Status |
| --- | --- |
| `sceVideoOutSysReadCrc64` | source-verified; returns a frame **checksum**, not pixels |
| `sceShellCoreUtil*GameLiveStreaming*` / `*SharePlay*` | source-verified; **orchestration/status/CPU-budget**, no pixels |
| Hardware encoder library | no verified name or signature; **unknown** |
| Cross-process framebuffer read | **forbidden** by AKENO's rules; not attempted |

## What would raise a row to "hardware-tested"

Only a real on-console result, recorded by a person after running the diagnostic (and, for any
function, only after a separately-approved, documented test that first establishes the signature
from a verified source). The diagnostic alone can reach at most **signature-verified** (a name
resolved), which is explicitly *not* proof the function is callable or does what its name implies.
