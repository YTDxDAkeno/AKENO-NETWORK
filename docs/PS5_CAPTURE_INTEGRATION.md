# PS5 capture diagnostic — integration notes

For merging the `feature/akeno-ps5-direct-capture` branch with the other branches (SharePlay
Alpha 0.5, and the parallel PS5 profile / controller quick-access / AudioLab v2 work).

## Branch and base

| | |
| --- | --- |
| Branch | `feature/akeno-ps5-direct-capture` |
| Base | `feature/akeno-party-shareplay` @ **`d32f0ca`** (SharePlay Alpha 0.5, CI green) |
| History | Linear on top of the SharePlay branch; all SharePlay commits preserved |
| Draft PR | into `feature/akeno-party-shareplay` (not production) |

This branch is **additive and isolated**, modelled exactly on the existing audio lab: a separate
experimental ELF behind a build flag. The release AKENO ELF is byte-for-byte unaffected by the
capture-diagnostic code (it is all under `#ifdef AKENO_CAPTURE_DIAG`).

## New files (no conflict possible)

```
clients/ps5/app/capture_diag.h, capture_diag.c        the read-only report builder + catalogue
clients/ps5/tests/unit/test_capture_diag.c            unit tests
docs/PS5_DIRECT_CAPTURE_RESEARCH.md                   research (tracks A-E)
docs/PS5_CAPTURE_CAPABILITIES.md                      capability matrix
docs/PS5_CAPTURE_DIAGNOSTIC_TEST.md                   hardware test plan
docs/PS5_DIRECT_SHAREPLAY_PLAN.md                     prioritised plan + blockers
docs/PS5_CAPTURE_INTEGRATION.md                       this file
```

## Modified shared files (all additive; keep both sides on merge)

| File | Change | Guarded by | Conflict risk |
| --- | --- | --- | --- |
| `clients/ps5/app/platform.h` | capture probe prototypes | `#ifdef AKENO_CAPTURE_DIAG` | low |
| `clients/ps5/app/platform_ps5.c` | capture probes (dlopen confined here; read-only) | `#ifdef AKENO_CAPTURE_DIAG` | low |
| `clients/ps5/app/platform_host.c` | host simulation of the probes (env-driven) | `#ifdef AKENO_CAPTURE_DIAG` | low |
| `clients/ps5/app/app_routes.c` | `GET /api/diagnostics/capture`, `POST …/resolve`, `captureDiagnostic:true` in state | `#ifdef AKENO_CAPTURE_DIAG` | **medium** — AudioLab also edits app_routes.c; both are separate `#ifdef` blocks, keep both |
| `clients/ps5/app/web/app.js` | `captureDiagSection()`, shown when `d.captureDiagnostic` | runtime flag | **medium** — profile/quick-access/AudioLab also edit app.js; keep all sections |
| `clients/ps5/CMakeLists.txt` | `akeno-app-capturediag` host exe; `capture_diag.c` + `test_capture_diag.c` in unit tests | — | medium — keep alongside the audiolab exe |
| `clients/ps5/console/Makefile` | `CAPTURE_DIAG=1` variant → `akeno-network-ps5-capture-diagnostic.elf` | — | medium — a new `else ifeq` branch next to `EXPERIMENTAL_AUDIO` |
| `clients/ps5/tests/unit/test_main.c` | `run_capture_diag_tests()` | — | low |
| `clients/ps5/tests/integration/run.mjs` | `--capturediag` option + host-sim steps | — | low |
| `.github/workflows/ps5.yml` | build + import-gate + checksum + artifact the diagnostic ELF | — | medium — a new step block |

Enum names use the `AKENO_CAPLV_` / `AKENO_CAPROLE_` prefixes to avoid colliding with
`akeno/caps.h` (`AKENO_CAP_*`); keep that distinction on merge.

## Import safety

The capture-diagnostic ELF adds **no new strong system imports**. Its only run-time loading is
`dlopen`/`dlsym`, confined to `platform_ps5.c` (as in the audio lab) and run only in the RESOLVE
tier. `clients/ps5/console/check-imports.sh` is run against it in CI and must list the **same
system imports as the release ELF**. Nothing resolved is ever called.

## API / schema / config

- No server API change, no database change, no schema change, no migration, no new environment
  variable. This is console-side only.
- New console-local routes (`/api/diagnostics/capture[/resolve]`) exist **only** in the
  capture-diagnostic build; the release app returns 404 for them (asserted in the integration
  tests).
- Host simulation is driven by `AKENO_SIM_CAPTURE_*` env vars (tests only); clearly labelled as
  simulation, never presented as a console result.

## Merge procedure

1. Do not rebase any branch. From an integration branch off `feature/akeno-party-shareplay` (or
   the combined SharePlay+profile branch), `git merge --no-ff feature/akeno-ps5-direct-capture`.
2. Resolve the `app_routes.c`, `app.js`, `CMakeLists.txt`, `Makefile` and `ps5.yml` touch points
   by **keeping both sides** — every capture addition is a distinct block.
3. Build and check: `pnpm test` (shared/server/web unchanged), `pnpm test:client` plus
   `--capturediag`, and the PS5 workflow (release, audiolab and capture-diagnostic ELFs must all
   pass the import gate with the same system imports).

## Relationship to the other PS5 work

- **AudioLab v2**: same pattern (separate experimental ELF). The two do not interact; both edit
  `app_routes.c`/`app.js` in separate guarded blocks.
- **Profile / quick-access**: UI additions in `app.js`/`index.html`; the capture-diagnostic
  section is additive and shown only in its own build.
- The stable release ELF and the SharePlay Alpha 0.5 system are unchanged.
