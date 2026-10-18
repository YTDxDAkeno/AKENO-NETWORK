# Handoff — AKENO NETWORK Native (PS5), 1.0.0-beta.1 and 1.1.0-beta.1 (voice)

_Written 2026-10-18 for the next person or session. Facts only; anything not run is marked so._

## Where things are

| Item | Value |
| --- | --- |
| Branch | `claude/affectionate-cannon-s38t7k` (continues PR #7's head `943c28b` by fast-forward; no rebase, no force push) |
| PR #7 | [YTDxDAkeno/AKENO-NETWORK#7](https://github.com/YTDxDAkeno/AKENO-NETWORK/pull/7), draft, branch `claude/intelligent-goodall-f0fn0g` @ `943c28b`, base `claude/laughing-ptolemy-op1d4e`. **Not changed** by this work; the 1.0 work lives only on the branch above |
| `main`, production | untouched. GitHub `main` is not production. VPS, database, deployed Account Bridge, DNS: not touched, not reachable from this environment |
| Version | AKENO Native `1.0.0-beta.1` (`clients/ps5-native/src/app/app.h`); daemon still reports `0.5.0-alpha` to the server |
| Approved test candidate | commit `238aec4`, run [39085172366](https://github.com/YTDxDAkeno/AKENO-NETWORK/actions/runs/39085172366), artifact `akeno-network-native-1.0.0-beta.1` (`eboot.elf` `f3b59cde…`, daemon `fc37e9d5…`); approved by the owner for hardware testing on 2026-10-18. Later commits change documentation only and produce different build IDs: test **this** artifact |
| Release package | CI artifact **akeno-network-native-1.0.0-beta.1** of the "PS5 native client" workflow (layout: [native/INSTALL.md](native/INSTALL.md)) |
| Hardware status | Native 0.1 `d094e62bcaf1` and the Alpha 0.5 RC daemon ran on a PS5 with firmware 12.20. Native 0.2 ran on the owner's PS5 (12.20): screenshots of real friend profiles and game activity (owner report 2026-10-18; build ID not recorded) — partial coverage only. **1.0.0-beta.1, the protocol-2 daemon and the In-Game Companion have never run on a PS5** |

## Commits of this work (after `943c28b`)

| Commit | Content |
| --- | --- |
| `4506251` | E2E: assertions for the redesigned profile (the "Friends" pill and empty states); the failing step of `943c28b` fixed without weakening it |
| `57df2a5` | One artwork resolver (verified server art → admin media → cache → installed icon → placeholder, never by name); vector renderer float-overflow fix found by fuzzing; libFuzzer CI job; stb_image and Lucide licences |
| `a344555` | Daemon IPC protocol 2 (write list, focus route); chat, keyboard and party models with unit tests |
| `ac69db2` | E2E for IPC 2: allowed and refused writes, focus route, protocol version, logs |
| `255b053` | Native screens: Messages, Chat, controller keyboard, Party, Inbox, Settings, picker, confirmations |
| `f1a78b7` | E2E: an uninstalled game shows its administrator-verified cover |
| `9f28167` | In-Game Companion (experimental OnionHEN plugin) and the in-game research |
| `774b896` | Version 1.0.0-beta.1 |
| `07cf6b3` and later | Release package layout, fallbacks, guides, test plan, documentation |

## What works (PC evidence)

- **1,329 unit checks** (ASan/UBSan, Valgrind), **120 UI integration checks** (headless SDL,
  virtual controller, fake daemons incl. an IPC-1 daemon that still receives GET only).
- **End to end against the real server** (PostgreSQL, the server build, TLS with a throwaway CA,
  the native-IPC daemon host build, the release daemon): friends and live updates; typing "Hi 88"
  with the controller keyboard and finding exactly one copy on the server; read marking; no daemon
  notification while the chat is open and one after leaving; live party invitation accepted;
  party chat; leaving; friend request accepted; settings toggle; presence change; refused writes
  (query strings, `..`, nested and encoded segments, over-long IDs, routes off the list); the
  uninstalled game's cover; the In-Game Companion reading the menu and making its two writes;
  exactly one realtime session per console.
- **libFuzzer** on the image and path decoders (90 s each in CI), clang-tidy clean.
- **PS5 builds**: AKENO Native, the native-IPC daemon and the companion plugin each built twice,
  byte-identical; import gates (AKENO Native: the same 51 reviewed system functions as before);
  the native-IPC daemon imports exactly what the release daemon imports; the release daemon of
  this tree is still byte-identical to the hardware-tested Alpha 0.5 RC; all fallbacks rebuilt from
  their commits and byte-identical to their CI files (0.1 both IDs, 0.2 + IPC-1 daemon, RC).
- The whole PS5 job, including packaging, was also run locally with the same commands.

## What needs the PS5 (owner)

Everything in [HARDWARE_TEST_PLAN_PS5_NATIVE_1.0.0-beta.1.md](HARDWARE_TEST_PLAN_PS5_NATIVE_1.0.0-beta.1.md)
(B1–B10): the protocol-2 daemon, the social screens, the keyboard on a real DualSense, the
notification suppression, artwork, stability, and the 0.1 leftovers (every button, clean exit).
The In-Game Companion has its own nine-point proof in [IN_GAME_ACCESS.md](IN_GAME_ACCESS.md).

## Decisions only the owner can make

1. **Approve a build for the hardware test** (a specific CI run of a specific commit). Builds
   are never sent to a console automatically; the older `943c28b` build must not be used.
2. **OnionHEN**: whether to install it at all (it injects into ShellUI, bundles kstuff but skips it
   when one runs, deletes a downloaded system update at start). Only then the companion plugin.
   Never two kstuff/HEN stacks.
3. **Release**: whether to tag `native-1.0.0-beta.1` and publish a GitHub release (as a
   **pre-release draft**, never "stable" without approval); whether PR #7 should be updated to
   this branch or a new PR opened.
4. Microphone or voice tests need explicit approval; none exist in this build.

## Running the tests locally

```sh
clients/ps5-native/scripts/fetch-deps.sh && clients/ps5-ingame/scripts/fetch-deps.sh
cmake -S clients/ps5-native -B clients/ps5-native/build-asan -DAKN_SANITIZE=ON && cmake --build clients/ps5-native/build-asan
ctest --test-dir clients/ps5-native/build-asan
node clients/ps5-native/tests/integration/run.mjs clients/ps5-native/build-asan/akeno-native
cmake -S clients/ps5 -B clients/ps5/build-asan -DAKENO_SANITIZE=ON -DAKENO_REQUIRE_MBEDTLS=ON
cmake --build clients/ps5/build-asan --target akeno-app akeno-app-native-ipc
cmake -S clients/ps5-ingame -B clients/ps5-ingame/build -DAKN_SANITIZE=ON && cmake --build clients/ps5-ingame/build
pnpm install && pnpm build    # the server for the end-to-end test
TEST_DATABASE_ADMIN_URL=postgres://user:pass@127.0.0.1:5432/postgres \
  node clients/ps5-native/tests/e2e/run.mjs --native clients/ps5-native/build-asan/akeno-native \
  --daemon clients/ps5/build-asan/akeno-app-native-ipc --release-daemon clients/ps5/build-asan/akeno-app \
  --companion clients/ps5-ingame/build/akeno-ingame-check
```

PS5 builds: `PS5_PAYLOAD_SDK=<sdk f5d40f0>` with `clients/ps5-native/scripts/build-sdl-ps5.sh`,
`make -C clients/ps5-native/console`, `make -C clients/ps5/console NATIVE_IPC=1`,
`make -C clients/ps5-ingame/console` (see the workflow for the exact gates).

## Rules that stay in force

- The release daemon ELF must stay byte-identical to the hardware-tested RC (`ps5.yml`); daemon
  changes only under `#ifdef AKENO_NATIVE_IPC`, with identical imports.
- AKENO Native never pairs, unpairs, writes `/data/akeno/`, or changes the server address, console
  name, Account Bridge or daemon secrets; the daemon's write list is the only way to change
  anything, and the server re-checks scopes.
- No second realtime session; no system notifications from AKENO Native.
- No covers by name matching; no scraping of Sony endpoints; no third-party artwork redistributed
  without rights.
- No microphone use, no claims about voice on the PS5 without hardware evidence.
- No injection into games, ShellUI patches, kernel patches or system-file changes by AKENO.
- Fallbacks stay in every package and are byte-verified in every run.

## Suggested next steps

1. Owner hardware test of 1.0.0-beta.1 (B1–B10), then fix what it finds.
2. With results: record them in a `PS5_NATIVE_HARDWARE_RESULTS` document and update the status
   tables (`PS5_NATIVE_CLIENT.md`), then decide about the tag and pre-release.
3. Parity review against the browser page (Phase 2.5); the page stays until the owner decides.
4. Only after an approved OnionHEN setup: the companion's hardware proof.

## 1.1.0-beta.1: two-way party voice (branch `claude/native-voice`, successor of PR #8's branch)

| Item | Value |
| --- | --- |
| Branch | `claude/native-voice`, started from `34c2485` (draft PR #8's head); PR #8 and the 1.0.0-beta.1 candidate (`238aec4`, run 39085172366) are unchanged |
| Design | [PS5_VOICE.md](PS5_VOICE.md): Opus gateway (apps/voice-gateway) chosen over a WebRTC stack on the console |
| Server | `POST /parties/:id/voice/device-token` (`parties:write`), device-bound revocation; `VOICE_GATEWAY_URL` |
| Gateway | `apps/voice-gateway` (LiveKit Node SDK, Opus, mixing); compose `voice` profile service, Caddy `/voice-gateway`; **not deployed**, image not built here (no Docker daemon in this environment) |
| Daemon | native interface 3 (voice pass on the write list); release ELF unchanged (byte-identical RC) |
| Console | Party: Join/Leave voice, mute, deafen, volumes, speaking; Settings: microphone test; libSceAudioIn via dlopen after consent; libopus 1.5.2 |
| Evidence | PC only: unit, server, gateway with real LiveKit, end to end with LiveKit + gateway + daemon + AKENO Native + a LiveKit participant, both directions by frequency; PS5 build and gate |
| Needs the PS5 | everything in [HARDWARE_TEST_PLAN_PS5_NATIVE_1.1.0-beta.1.md](HARDWARE_TEST_PLAN_PS5_NATIVE_1.1.0-beta.1.md); first of all whether libSceAudioIn opens on 12.20 (V1) |
| Owner decisions | deploying the gateway (test setup first), approving the 1.1 build for the hardware test, the in-game voice research (PS5_VOICE.md, last section) |

Run the voice tests locally: download LiveKit 1.12.0 (`livekit_1.12.0_linux_amd64.tar.gz`, SHA-256
`9b89eb16…3aee`), then `LIVEKIT_SERVER_BIN=<path> pnpm --filter @akeno/voice-gateway test` and the
end-to-end command above with `--livekit <path>` (after `pnpm build`).
