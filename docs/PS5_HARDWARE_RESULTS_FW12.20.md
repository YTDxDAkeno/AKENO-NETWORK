# PS5 Alpha 0.5 — hardware results on firmware 12.20

**Real hardware evidence.** Reported by the console owner on 2026-10-17: PS5, firmware **12.20**,
kstuff, payloads sent with the owner's loader (elfldr). Builds: `akeno-network-ps5.elf` and
`akeno-network-ps5-audiolab-v2.elf` from the Alpha 0.5 release candidate (PS5 console build of
`7e28255`). Only what was reported is recorded here; steps not mentioned stay **untested**.

| Area | Test plan | Result on 12.20 | Status |
| --- | --- | --- | --- |
| Profiles | A6–A11 | Profile interface works: real avatar, real banner, badges, game covers, gaming activity | **Verified on hardware** |
| Controller shortcut (Quick Access) | B2–B9 | Failed every time, for **all three** combinations. `scePadOpen` returned **`0x809B0081`** each time, before any controller sample was read | **Does not work** (background payload) |
| Native audio output (Audio lab v2) | C1–C5, C7 | Lab started. Report: `sceAudioOutInit` succeeds; `sceAudioOutOpen` succeeds (system user 0xFF, main port); every completed `sceAudioOutOutput` call succeeds; nonzero PCM submitted; calls paced at about playback speed. **None** of the completed tests with user feedback was audible — formats, frames per call, levels and the delayed (home screen) test | **Accepted and paced, not audible** |
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
- **Accepted, paced, not audible:** the report rules out an API error, an unsupported format and
  discarded samples; an audio clock consumes AKENO's port, but its sound does not reach the TV or
  headset. Why is not known — see [PS5_AUDIO_RESEARCH.md](PS5_AUDIO_RESEARCH.md).
- The audio-focus hypothesis is **not confirmed** by these results.
- PS5 voice chat does **not** work and is not claimed.

## Next

Audio lab v3 ([test D](HARDWARE_TEST_PLAN_PS5_ALPHA_0.5.md#test-d--audio-lab-v3-read-only-port-state-and-browser-audio-home-screen-only))
reads the state of AKENO's own audio port and tests the browser's `<audio>` element. Its report
file contains everything needed, including the browser feature table.

## Unchanged by these results

The controller shortcut stays **off by default**; nothing else in the release ELF depends on it.
No server, VPS or production change was made.
