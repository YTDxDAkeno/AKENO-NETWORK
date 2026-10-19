# AKENO Party Voice: party voice on the PS5 while a game runs (Phase 4.3, EXPERIMENTAL)

Status (2026-10-19): **built and tested on PCs end to end; not run on a PS5.** The voice service,
the plugin's voice page and the package exist. Whether the PS5 lets a background payload capture
the microphone and be heard while a game runs is unknown and is exactly what the owner's hardware
test decides ([shellui/voice-alpha/VOICE-TEST-DE.md](shellui/voice-alpha/VOICE-TEST-DE.md)). The
production server cannot accept console voice yet (voice pass: HTTP 404 on 2026-10-18): the server
upgrade comes first ([shellui/voice-alpha/SERVER-PREREQUISITES-DE.md](shellui/voice-alpha/SERVER-PREREQUISITES-DE.md)).

## What the owner does

1. In a game: hold **L2 + R3** (Phase 4.1 host, unchanged) → AKENO NETWORK → **Voice chat**
   (main page) or **Party → Voice chat**.
2. **Join voice chat** (the PS5 asks first; the microphone stays off).
3. AKENO joins the party's voice room through the existing voice gateway; friends on PCs and
   phones see the console join. Incoming voice is decoded and sent to the console's audio output.
4. **Turn on microphone** (asks first) → the party hears the console, if the PS5 allows capture.
5. **○** back to the game; the voice service keeps running in the background.
6. L2 + R3 again: mute, deafen, volumes, speaker route, test sound, leave.

The game is never closed or replaced. ShellUI and the game get no audio code.

## Architecture

```
PS5 (game in front)
  onionHEN host (Phase 4.1, unchanged) -- L2+R3 --> Settings: AKENO pages of plugin AKEN00001 1.20
  AKEN00001 (plugin process, no audio) --127.0.0.1:8646, daemon's native key--> akeno-party-voice
  akeno-party-voice (background payload, its own process)
     network  voice pass:  POST /parties/:id/voice/device-token --> AKENO daemon (IPC 3, unchanged) --> AKENO server
              wss://<AKENO server host>/voice-gateway (TLS, daemon CA) <-- Opus both ways --> voice gateway --> LiveKit
     speaker  jitter buffer -> Opus decode/concealment -> volume -> sceAudioOut (route 1-4) ; paced by the port or the clock
     mic      only after "Turn on microphone": sceAudioIn (dlopen, 16 kHz) -> 48 kHz -> gain -> Opus -> gateway
     report   /data/akeno-party-voice/report.txt, voice.log (states, codes, counts), settings.json
AKENO server + voice gateway + LiveKit (the existing voice system; browsers/apps join LiveKit directly)
```

| Part | Change |
| --- | --- |
| Phase 4.1 onionHEN host | **none** (rollback stays the original v0.9.0) |
| AKENO daemon (native-IPC, interface 3) | **none**: its write list already contains the voice pass route (`196ca13c71d1-ps5-native-ipc` and newer) |
| Plugin AKEN00001 | **1.20**: Voice chat page and speaker-route page, voice lines on the diagnostics page, entries on the main and party pages; talks to the voice service. 1.10 is rebuilt byte-identically from `f801370` as the rollback |
| `akeno-party-voice.elf` | **new** background payload (`clients/ps5-partyvoice`) |
| Server, gateway | **none in this phase**; the existing code of this branch line is the requirement |

### Why a separate process and the plugin as the remote control

- The plugin is a background process too and gets no audio rights from onionHEN; a failed audio
  attempt must never take messages and the party page with it.
- A separate payload can crash, be stopped (Diagnostics → *Stop the voice service*) or be sent
  again without touching ShellUI, the game, the daemon or the host. It never restarts itself and
  nothing restarts it (no recovery loops).
- The plugin authenticates with the AKENO daemon's per-launch native key (`/data/akeno/native.key`,
  owner-only), the same key it already uses for the daemon. No daemon change was needed: the
  daemon's interface 3 already proxies the voice pass.

## The voice service

| Topic | Behaviour |
| --- | --- |
| Start | ELF loader port 9021 after the daemon, or the launcher entry (`daemon: true`). Waits on 127.0.0.1:8646; opens nothing (no audio port, no microphone, no connection beyond 127.0.0.1) until the user joins. Refuses to run twice (the port) |
| Join | Plugin → `POST /api/voice/join {"partyId"}` → voice pass through the daemon (the server checks membership, blocks, device scope `parties:write`) → the gateway must be on the AKENO server's own host → TLS WebSocket → `hello` → `ready`. The output port opens at once (listen-only); the microphone stays closed |
| Microphone | Only after **Turn on microphone** (a confirmation on the PS5). `sceAudioInOpen(foreground user, type 1, 256 frames, 16 kHz; 48 kHz fallback)` exactly as AKENO Native 1.1 (works there in the foreground on 12.20). Mute keeps the port open and sends nothing (the gateway unpublishes the track); *Turn off microphone* closes it. The permission does not survive a restart |
| Speaker | `sceAudioOut` S16 stereo 48 kHz, 1024 frames. Route 1 = system user, main port (audible from the foreground on 12.20); routes 2-4 = the Voice Lab's foreground-user, voice and personal port variants. A route change reopens the port. **N beeps on route N** (test sound) |
| Pacing | Every output call is timed. Calls that block ≈ one block = "paced" (an audio clock took them); calls that return at once are paced by the clock instead and reported **NOT paced** |
| Failures | Mic open/read failures → "Microphone unavailable" with the code; voice stays connected (listen-only). Output open/50 failed writes → "Audio output unavailable" with the code; incoming audio is still decoded in real time (counted) and the microphone still works (talk-only) |
| Reconnect | Lost connection → new voice pass and connection after 1, 2, 4, 8, then every 10 s, up to 30 tries (≈5 min). Not after removal (4003), replacement (4004), room end (4005), a refused or missing voice pass (404, 403, 503) |
| Leave / exit | Leave closes microphone, output and connection; *Stop the voice service* also ends the process |
| Notifications | System notifications for ready, joined, reconnecting, microphone on/unavailable, output unavailable, who joined or left (at most one every 4 s), and why voice ended |
| Limits | volume 0-150 %, microphone gain 50-150 % (saturating), 240 ms send queue (oldest dropped), jitter 60-200 ms, 4 KiB control bodies |
| Privacy | Nothing is recorded or stored. Report and log: states, codes, counts, levels; never names, IDs, voice pass, key or audio |

### The local control interface (protocol 1)

`127.0.0.1:8646`, header `X-Akeno-Native-Key` = the daemon's current key (read from the file on
every request; a key file that is not private is refused), `Host` must be the loopback address,
any `Origin` header is refused (web pages), bodies are JSON objects with exactly the listed members
and types. `GET /api/voice/hello` (no key), `GET /api/voice/status`, `POST /api/voice/join |
leave | mic | mute | deafen | volume | route | test-sound | exit`
([pv_control.h](../clients/ps5-partyvoice/src/pv_control.h)).

### What the report separates

| Outcome | Means | Still needs |
| --- | --- | --- |
| **NETWORK CONNECTED** | the gateway accepted the voice pass, said ready and sent the participants | — (the friend sees the console in voice) |
| **MICROPHONE TRANSMITTING** | frames that were not exact zeros were captured, encoded and sent; the gateway's level of the console's decoded audio is shown | the friend's answer: "I hear you" |
| **SPEAKER OUTPUT ACTIVE** | incoming audio was decoded and the output port took blocks with sound | the owner's answer: "I hear my friend" (and on which route) |

Plus: heartbeat gaps (process paused?), seconds of connected voice while a game ran, CPU time,
packet counts, every open/read/write code. The report ends with the owner's four answers.

## Safety (enforced by `console/check-partyvoice.sh` on every build)

- 15 system imports, every one already used by an AKENO payload on 12.20 (daemon, AKENO Native,
  Voice Lab): `sceAudioOutInit/Open/Output/Close`, `sceKernelSendNotificationRequest`, the resolver,
  `sceSystemServiceGetAppIdOfRunningBigApp` (weak, read-only), `sceUserServiceInitialize`,
  `sceUserServiceGetForegroundUser` (weak).
- Run-time loading: only `libSceAudioIn.sprx` with `sceAudioInOpen/Input/Close`, only after the
  user turned the microphone on.
- No audio focus, attach/detach, app-module, volume, main-output, exclusive-HDMI or chat function is
  imported **or named** anywhere; no app launch/kill, mount, ptrace, kernel helper, pad, registry,
  voice-chat library; only `pv_backend_ps5.c` calls console functions.
- Writes only `/data/akeno-party-voice/`; reads `/data/akeno/` (key, settings, CA).
- No game injection, no ShellUI code, no Sony service change, no PSN, no kstuff, no second onionHEN.

## Testing: what is evidence of what

| Level | What | Where | Result |
| --- | --- | --- | --- |
| Unit (PC; ASan/UBSan, TSan) | report privacy and wording, status document, control security (key, Origin, Host, strict bodies, ranges), voice pass refusals (404 = server not upgraded, interface 2, 503, 403, 5xx retried), gateway on another host refused, microphone consent, mic refused / missing / failing, output refused / not paced / failing, route change, test sound, settings | `clients/ps5-partyvoice/tests` | pass |
| Plugin (PC; ASan/UBSan) | every voice state renders within onionHEN v0.9.0's rules; the largest document with the full voice page (236 of 256 nodes); every control routes to its exact command; status parsing; documents through the Phase 4.1 host's Toolbox link and direct root | `clients/ps5-ingame/tests`, `tools/onionhen/check-direct-entry.sh` | pass |
| End to end (PC) | real server + LiveKit 1.12 + gateway (TLS) + native-IPC daemon + voice service (simulated console) + the plugin's own code + a PC participant: join, the server shows the console; Bob → console decoded at 600 Hz and submitted; console → Bob decoded at 300 Hz; mute, unmute, deafen, undeafen; speaking indicators; volume/gain limits, route, test sound; 10 UI open/close cycles without disruption; 12 s "game running" with no UI open; network drop and reconnect; revocation (4003, not retried); leave and cleanup; leaving the party; crash and restart; microphone and output refused; report privacy | `clients/ps5-partyvoice/tests/e2e/run.mjs`, CI `ps5-voice.yml` | see the CI run |
| PS5 build | voice service and plugin built twice (byte-identical), import gates, plugin descriptor; plugin 1.10 rebuilt = `201d2c88…` | CI `ps5-voice.yml` | builds |
| PS5 hardware | everything about real audio | the owner | **not done** |

A simulated microphone and speaker prove the pipeline, not the console: the PC test says nothing
about whether the PS5 lets a background process capture or be heard.

## Known limits and the decision table after the hardware test

| Result on the PS5 | Next step |
| --- | --- |
| Friend hears the console, owner hears the friend, also in a game | keep; longer session; latency and echo |
| Mic transmits, but nothing audible on any route | **talk-only** while gaming; listen via phone/PC; research continues |
| Speaker audible on route N, mic all zeros / refused | **listen-only** while gaming |
| Neither | background voice is not available with public interfaces on 12.20; voice stays in AKENO Native (foreground) |
| `NOT paced` on every route | the port takes nothing from a background process (samples discarded) |
| Large heartbeat gaps in a game | the system pauses background payloads during games |
