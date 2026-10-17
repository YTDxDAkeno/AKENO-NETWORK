# Integrating PS5 Alpha 0.5 with the production line (Account Bridge)

Status 2026-10-17. **Nothing here has been merged into, pushed to or deployed on the production
line.** Alpha 0.5 stays on its own branch, `claude/laughing-ptolemy-op1d4e`, until the production
baseline is published on GitHub by the VPS-side session that owns it.

## The branches as they are on GitHub

```
ae94851  Alpha 0.4 (voice) ── claude/vibrant-mendel-v55zqf
   ├── 897bcf1 … PS5 Alpha 0.5 ─────────────────────── claude/laughing-ptolemy-op1d4e (this work)
   └── 65bd581 … d32f0ca  SharePlay ─────────────────── feature/akeno-party-shareplay
          └── 0bf5eb4 … cb2310d  Account Bridge ─────── claude/project-thread-cx3gpe
```

- The production commit `96d5405` and the production release tags are **not on GitHub** (no tags
  exist in the repository). Whether `cb2310d` is exactly what runs on the VPS is unknown; it is the
  newest Account Bridge commit that is published.
- Other branches with the same base (not part of this integration): `claude/akeno-lan-play-m58n1e`
  (LAN Play, based on SharePlay), `feature/akeno-ps5-direct-capture` (capture diagnostic).
- Alpha 0.5 and the Account Bridge line share the base `ae94851`. Alpha 0.5 changes only the
  console app (`clients/ps5`), the PS5 workflow and docs; the Account Bridge line changes the
  server, web, shared package and console app, and **no workflow**.

## Trial integration (local only, not pushed)

`origin/claude/project-thread-cx3gpe` (`cb2310d`) merged with Alpha 0.5, in a throwaway
worktree:

| Check | Result |
| --- | --- |
| Conflicts | 9 files, all of the kind "both sides appended to the same list"; no logic conflict |
| Unit tests (OpenSSL and mbedTLS) | 743 checks, 0 failures (SharePlay + Account Bridge + shortcut + audio PCM) |
| Host builds (ASan) | release and audio lab apps build without warnings |
| PS5 builds | both ELFs build with `-Werror`, pass `check-imports.sh`; release imports identical to the hardware-tested `ae94851` build |
| End-to-end suite | see the PR description for the result of the trial run |

### Resolution, file by file (keep both sides)

| File | Production side (keep) | Alpha 0.5 side (keep) |
| --- | --- | --- |
| `clients/ps5/CMakeLists.txt` | `shareplay_rx.c`, `account_bridge.c`, `app_account.c`, `platform_host_account.c`, `test_shareplay.c`, `test_account_bridge.c`, `shareplay.js` dependency | `quick_access.c`, `shortcut.c`, `audio_lab_pcm.c`, `test_shortcut.c`, `test_audio_pcm.c`, `audiolab.js` dependency — union of both source lists in all four places |
| `clients/ps5/console/Makefile` | `platform_ps5_account.c account_bridge.c app_account.c shareplay_rx.c` | `quick_access.c shortcut.c` (and `LAB_SRC`) — one `APP_SRC` with all of them |
| `clients/ps5/app/app_internal.h` | `#include "platform_account.h"`, `"shareplay_rx.h"`, `akeno_ui_shareplay_js` | `#include "shortcut.h"`, `akeno_ui_audiolab_js` (inside `#ifdef AKENO_EXPERIMENTAL_AUDIO`) |
| `clients/ps5/app/ui_assets.c` | embed `shareplay.js` | embed `audiolab.js` only under `AKENO_EXPERIMENTAL_AUDIO` |
| `clients/ps5/app/app_routes.c` | `GET /shareplay.js` | `GET /audiolab.js` (empty stub in the release build) — two separate route blocks |
| `clients/ps5/app/web/index.html` | `<script src="/shareplay.js">` | `<script src="/audiolab.js">` — both, before `/app.js` |
| `clients/ps5/app/web/app.js` (1) | `shareplay: viewSharePlay` in `views` | `profile: viewProfile` in `views` |
| `clients/ps5/app/web/app.js` (2) | `accountBridgeSection(...)` in Diagnostics | `quickAccessDiagnostics(...)` and `audioLab.section()` — all three |
| `clients/ps5/app/web/app.js` (3) | `accountBridgeSection()` function | the old in-file `audioLabSection()` is **dropped** (it moved to `audiolab.js` in Alpha 0.5) |
| `clients/ps5/console/check-imports.sh` | comment and message lines for `platform_ps5_account.o` (libSceUserService/RegMgr/Remoteplay, read-only) | comment and message lines for `libScePad.sprx` (shortcut) — both |
| `clients/ps5/tests/unit/test_main.c` | `run_shareplay_tests`, `run_account_bridge_tests` | `run_shortcut_tests`, `run_audio_pcm_tests` |

Auto-merged without conflict (verify, don't hand-edit): `app.c`, `run.mjs`, `.github/workflows/ps5.yml`,
`AKENO_APP_VERSION` (becomes `0.5.0-alpha`).

Interactions checked: the Account Bridge's `platform_ps5_account.c` also loads libraries at run time;
the gate's "dlopen only in the platform layer" rule already allows it on that line. Alpha 0.5's new
gate rules (no `sceAudioIn` in platform code, no audio-lab strings in the release ELF) pass with the
Account Bridge code present. Both features look up `libSceUserService.sprx` with `dlopen`
independently; neither unloads it.

## Strategy once the production baseline is on GitHub

1. **The VPS-side session publishes** the production branch and its release tags exactly as
   deployed (no rewriting). Nothing in this session creates or force-pushes production history.
2. **Compare** the published production head with `cb2310d`:
   `git diff cb2310d <production-head> -- clients/ps5 .github`. If it equals or extends `cb2310d`
   in other areas only, the resolutions above apply unchanged.
3. **Integrate on a new branch** from the production head (e.g. `integration/alpha-0.5`): merge
   `claude/laughing-ptolemy-op1d4e` (a merge, not a rebase, so both histories stay intact),
   resolve with the table above, then run: `pnpm lint`, `pnpm typecheck`, `pnpm test`, the C unit
   tests (ASan and Release + Valgrind), the full `run.mjs` suite with `--app --audiolab`, both PS5
   builds and the import gate.
4. **Feature-preservation checklist** (all must stay green/present): Account Bridge (link,
   read-only check, unlink; its unit and E2E tests), device pairing, parties, LiveKit voice (web
   E2E), notifications (plain and with button), game detection and playtime, existing profiles on
   the website, SharePlay.
5. **Open the PR** from the integration branch into the production branch; the production owner
   merges and deploys. The console app needs no server change for Alpha 0.5, so the server
   deployment is unaffected by this PR.
6. Until then, Draft PR "PS5 Alpha 0.5" targets `claude/vibrant-mendel-v55zqf` only so its diff
   shows exactly the Alpha 0.5 changes; it is not meant to be merged there.
