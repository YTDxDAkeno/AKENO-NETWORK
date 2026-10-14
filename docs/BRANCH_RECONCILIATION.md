# Branch reconciliation (Alpha 0.2)

Recorded 2026-10-14 (session 4). Nothing was force-pushed, no branch was deleted, `main` was not
changed.

## Branches found

| Branch | Tip | Content | CI at tip |
| --- | --- | --- | --- |
| `main` | `d5a07d8` | "Initial commit" (README only); ancestor of every other branch | — |
| `claude/adoring-johnson-6re0yl` (**Branch A**) | `133775c` | Alpha 0.1 console app (browser UI on 127.0.0.1:8642, mbedTLS HTTPS/WSS, native notifications) — **the build that ran on a PS5 with 12.20** is its commit `592c23a`; then server and website game activity (`2f3de26`, `771f09d`) and an unfinished detection checkpoint (`133775c`) | CI red (typecheck, host C), PS5 link failed |
| `claude/epic-bell-s1psav` (**Branch B**) | `2e70897` | Alternative native payload: `libSceHttp2` HTTP backend, native toasts, `libScePad` input, notification polling, event watermark/formatter | CI green (Linux only); its payload is **not built in CI** and never ran on hardware |
| `chatgpt/ps5-http-backend` (**PR #1**, open) | `e324fa6` | Diagnostic ELF (ran on 12.20) and an HTTP transport split | — |
| `claude/elegant-wright-4qi8gh` (this session) | — | Alpha 0.2 integration | green (see PROJECT_STATUS.md) |

Shared ancestor of A, B and PR #1: `8105593` ("Document device pairing, the device client, safety
rules and PS5 findings").

## Decisions

1. **Integration base = Branch A at `133775c`.** It contains the hardware-proven Alpha 0.1 and
   the server/website activity work. The session branch (at `d5a07d8`, an ancestor) was
   fast-forwarded to it; history is preserved, no merge or rebase was needed.
2. **Alpha 0.1 stays the reference.** Its ELF is rebuilt from `592c23a` in every PS5 CI run and
   must match the hardware-tested SHA-256 `35f393c6…6b65` byte for byte; it ships next to each
   Alpha 0.2 build as the fallback. Alpha 0.2's import set may only differ from Alpha 0.1's by
   reviewed weak imports (`console/reviewed-imports.txt`).
3. **Branch B, component by component** (nothing merged; the branch stays available):

   | Component | Decision | Why |
   | --- | --- | --- |
   | `libSceHttp2` / `libSceSsl` HTTP backend (`src/ps5/httpclient_ps5.c`) | Not adopted | Would replace the hardware-proven mbedTLS transport; no WebSocket support (B polls instead); no documented trust-store or host-name semantics; never ran on a console |
   | Native toasts (`src/ps5/notify.c`) | Already in A | Same `sceKernelSendNotificationRequest` call A uses and that is proven on 12.20 |
   | Controller input (`src/ps5/pad.c`, `libScePad`) | Not adopted | A background payload would compete with the running game for the controller; no reviewed sample shows safe shared use; unverified button layout. Recorded in QUICK_ACCESS.md |
   | Notification polling + watermark (`src/events.c`) | Not adopted | A uses the realtime WebSocket (proven) and needs no polling |
   | English notification formatter | Not adopted | A has its own, covering the same event types |
   | `sce.h` prototypes (from SDK samples) | Used as a cross-check only | A's prototypes come from the same samples |
   | Payload build system (`payload/Makefile`) | Not adopted | A's `console/Makefile` + import gate is the reviewed build |
   | SDK research notes | Folded into the docs | `sceNotificationSend`, `libScePad`, `libSceHttp2` findings are in QUICK_ACCESS.md / PS5_CLIENT.md |

4. **PR #1:** `e324fa6` (diagnostic ELF) was already adopted unchanged in A as `4fd58dc`; the
   HTTP method allow-list from `f9df326` was adopted in A, the transport split was not (reasons
   in PROJECT_STATUS.md, session 3). Nothing else to take; the PR can be closed as superseded
   when you decide. It was not merged.
5. **Checkpoint `133775c` repairs** (all in commit `2a2ad2d`): typed error responses in
   `activity.test.ts`; explicit path formatting in `game_detect.c`; a bounded raw-buffer copy in
   `platform_host.c`; `game_detect.c`/`activity.c` added to the console build; `sceNotificationSend`
   moved from a link-time import to opt-in run-time loading; `sceKernelGetAppInfo` made a weak
   import; `strnlen` replaced to keep the proven import set.
