# PS5 Alpha 0.5 — hardware results on firmware 12.20

**Real hardware evidence.** Reported by the console owner on 2026-10-17: PS5, firmware **12.20**,
kstuff, payloads sent with the owner's loader (elfldr). Builds: `akeno-network-ps5.elf` and
`akeno-network-ps5-audiolab-v2.elf` from the Alpha 0.5 release candidate (PS5 console build of
`7e28255`). Only what was reported is recorded here; steps not mentioned stay **untested**.

| Area | Test plan | Result on 12.20 | Status |
| --- | --- | --- | --- |
| Profiles | A6–A11 | Profile interface works: real avatar, real banner, badges, game covers, gaming activity | **Verified on hardware** |
| Controller shortcut (Quick Access) | B2–B9 | Failed every time, for **all three** combinations. `scePadOpen` returned **`0x809B0081`** each time, before any controller sample was read | **Does not work** (background payload) |
| Native audio output (Audio lab v2) | C1–C5, C7 | Lab started. **None** of the completed tone tests with user feedback was audible — different formats, frames per call, levels and the delayed (home screen) test | **Not audible** |
| Browser audio comparison | C6 | "API missing, state unavailable, heard −1" | **No Web Audio API** in the system browser |
| Other A/B steps (messages, notifications, game detection, party invite, B4/B6/B10, …) | A1–A5, A12–A17, B1, B4, B6, B10, B11 | not reported | untested |

What the results mean, and what they do **not** mean:

- **Profiles** are confirmed end to end: the console app's media proxy (`/api/media`, data: URLs)
  delivers the server's images to the PS5 browser under the page's strict policy.
- **`0x809B0081`** is not one of libScePad's own error codes (those are `0x8092xxxx`). The same
  value was returned to a different payload on firmware **13.60** (AnyPS5 issue #3177, see
  [QUICK_ACCESS.md](QUICK_ACCESS.md#controller-shortcut-hardware-result-and-analysis)). The
  meaning of the code is not publicly documented; "a background payload is not allowed to open a
  controller" is the leading explanation, **not** a documented fact.
- **"Browser API missing"** means that neither `window.AudioContext` nor
  `window.webkitAudioContext` exists in the 12.20 system browser (measured by the page itself;
  `heard −1` = no question was asked because there was nothing to play). It says nothing about the
  `<audio>` element, `getUserMedia` or WebRTC; the lab's read-only feature table lists those.
- **Not audible** does not yet say *why*. The detailed report (`/data/akeno/audiolab-report.txt`)
  separates API errors, unsupported formats, timing and "accepted but silent" — see
  [PS5_AUDIO_RESEARCH.md](PS5_AUDIO_RESEARCH.md#reading-the-audio-lab-v2-report).
- The audio-focus hypothesis is **not confirmed** by these results.
- PS5 voice chat does **not** work and is not claimed.

## Diagnostic output still needed (no new build)

1. `/data/akeno/audiolab-report.txt` (or photos of Diagnostics → Audio lab v2 → *Show report*),
   complete. It holds no tokens or account data.
2. The lab's **browser feature table** (Web Audio, *Audio element (WAV)*, getUserMedia,
   RTCPeerConnection).
3. Diagnostics → **Quick access**: `initResult`, user source, user ID, `openResult`.

## Unchanged by these results

The controller shortcut stays **off by default**; nothing else in the release ELF depends on it.
No server, VPS or production change was made.
