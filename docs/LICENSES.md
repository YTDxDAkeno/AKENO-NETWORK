# Licenses

## This project

AKENO NETWORK is released under the **MIT License** ([LICENSE](../LICENSE)). The repository
owner may choose a different license; MIT was selected as a permissive default for an original
codebase.

All source code, the logo mark, the avatar set (gradient badges using Lucide icons) and the
interface design were written for this project. **No code was copied** from Omega Dashboard,
OrbisNet, BlueSphere, CutTheCord or any other referenced project. No Sony or PlayStation
logos, trademarks, fonts, sounds or other proprietary assets are used. "PlayStation 5" and
other platform names appear only as plain-text, self-declared platform labels on game
sessions and in documentation.

## Device client and console app (`clients/ps5`)

All C sources (JSON, HTTP, WebSocket, SHA-1/Base64, client, both TLS transports, console app,
local page server, page HTML/CSS/JS, CLI, tests) were written for this project. The Linux build
links the system **OpenSSL 3** library (Apache-2.0) dynamically.

The console build additionally contains, downloaded at pinned versions with SHA-256 checks by
`clients/ps5/scripts/fetch-deps.sh` (not vendored in Git):

| Component | Version | License | Use |
| --- | --- | --- | --- |
| mbedTLS | 3.6.7 (LTS) | Apache-2.0 | TLS 1.2/1.3 and X.509 verification (statically linked) |
| Mozilla CA certificate bundle, via certifi | 2026.07.22 | MPL-2.0 | Trusted roots embedded in the ELF |
| ps5-payload-sdk (crt, libc, stubs) | `f5d40f0` | GPLv3+ (FreeBSD headers under BSD licenses) | Startup code and libc linked into the PS5 ELF |
| Lucide icons (path data of 8 icons, from lucide-react) | 1.56.0 | ISC | Badge medallion icons in the console page (`app/web/app.js`, embedded in both PS5 ELFs); notice below |

Because the PS5 ELF links the SDK's GPLv3+ runtime, a distributed PS5 binary is a combined work
under the GPLv3; AKENO's MIT and mbedTLS's Apache-2.0 code are compatible with that. The complete
corresponding source is this repository plus the pinned SDK and inputs named above. The PR #1
diagnostic ELF (`clients/ps5/ps5-smoke`) is built from the same SDK.

### Lucide icons in the PS5 console page (Alpha 0.5)

The console page draws AKENO's badge medallions with the same line icons as the website
(`apps/web/src/components/BadgeIcon.tsx`). Because the page has no package manager, the path data
of exactly these eight Lucide icons is copied, unmodified, into `clients/ps5/app/web/app.js`, which
is embedded in `akeno-network-ps5.elf` and the audio lab ELFs (`akeno-network-ps5-audiolab-v2.elf`, `-v3.elf`):

`crown` (AKENO Founder), `shield-check` (Administrator), `braces` (Developer), `badge-check`
(Verified Contributor), `sprout` (Early Supporter), `flask-conical` (Beta Tester), `gamepad-2`
(PS5 Alpha Tester), `ticket` (Community Event Participant).

None of them is among the icons Lucide lists as derived from Feather (MIT), so only Lucide's ISC
licence applies. Its full notice is reproduced in `app.js` next to the data (so it travels inside
every ELF), in this file, and in the build artifact (`LICENSES.md`):

```
ISC License

Copyright (c) 2026 Lucide Icons and Contributors

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
```

The hexagon medallion shape, colours and badge names are AKENO's own (`packages/shared/src/badges.ts`).

### Reference sources for the PS5 research (read, not linked into AKENO)

Function declarations used by the PS5 platform layer were taken from these projects; no code from
them is compiled into AKENO except the declarations' shapes and constants:

| Project | Commit | License | What was used |
| --- | --- | --- | --- |
| ps5-payload-dev/SDL (`src/audio/ps5`, `src/joystick/ps5`) | `ee4c47d` | zlib | `sceAudioOut*` and `scePad*` declarations, formats, button bits, `PS5_PadData` layout |
| ps5-payload-dev/websrv (`src/ps5/hbldr.c`) | `7748df9` | GPLv3+ | Declaration of `sceUserServiceGetForegroundUser`; read to understand how homebrew is launched (its method is **not** used) |
| ps5-payload-dev/elfldr | `02cfe91` | GPLv3+ | Read to understand how payloads are spawned (no code used) |
| boykopovar/AnyPS5 (`core/libs/prx/libSceAudioOut`, `libSceAudioIn`, `libScePad`, `libSceVoice`, `docs/dev/debt`, issue #3177) | `f517240` | GPL-2.0-or-later | Research only (2026-10-17): structure layouts, argument lists, error codes and a 13.60 hardware log are cited in the research docs. **No code copied**, nothing used in a build |
| sainsaji/PS5-Audio-Passthrough-Research (`docs/FINDINGS.md`, `sample-app/src/platform/ps5`) | `c3be9c2` | GPL-3.0 | Research only (2026-10-17): hardware results on firmware 12.70, call sequences and return codes cited in the research docs. **No code copied**, nothing used in a build |

| shadps4-emu/shadPS4 (`src/core/libraries/audio/audioout.h/.cpp`) | `e6c79a5` | GPL-2.0-or-later | Research only (audio lab v3): the `sceAudioOutGetPortState` declaration, the 32-byte `SceAudioOutPortState` layout and the output-state constants were compared; **no code copied** |
| red-prig/fpPS4 (`src/audio/ps4_libsceaudioout.pas`) | `04cefd4` | LGPL-2.1 | Research only (audio lab v3): same comparison; **no code copied** |
| OpenOrbis/OpenOrbis-PS4-Toolchain (`include/orbis/AudioOut.h`) | `0a1aaf9` | GPL-3.0 | Research only (audio lab v3): the two-argument declaration; **no code copied** |
| SvenGDK/SharpProspero (`src/SharpProspero/Interop/Audio/AudioOut.cs`) | `5dfdb4b` | GPL-3.0 | Research only (audio lab v3): the PS5 interop declaration with `Size = 32`; **no code copied** |

AKENO's `clients/ps5/app/audio_lab_state.c` declares the function itself from these facts
(`int32_t (*)(int32_t handle, void *state)`), treats the structure as raw bytes and decodes only
field offsets and constants — interoperability facts, not copied expression.

AKENO is MIT-licensed. Code from the GPL projects above could only be incorporated after a
licence review, because the combined ELF would then have to be distributed under the GPL. Only
facts needed for interoperability (function names, argument order, constants, observed return
codes) are recorded, with their source.

## AKENO NETWORK Native (`clients/ps5-native`)

All AKENO Native sources (core, SDL layer, platform layer, logo rasteriser, PNG writer, tests,
icon generator, launcher manifest) were written for this project (MIT). It reuses AKENO's own
`clients/ps5/src` client library, `clients/ps5/app/audio_lab_pcm.c` and `game_detect.c` unchanged.
The AKENO mark is drawn in code from the website's own SVG geometry. **No Sony fonts, sounds or
interface assets** are used, and the app does not read the console's system fonts.

Inputs of the PS5 build, pinned and checksummed by `clients/ps5-native/scripts/` (not vendored):

| Component | Version | License | Use |
| --- | --- | --- | --- |
| ps5-payload-dev/SDL | `ee4c47d` (2.30.12, PS5 port) | zlib | Window, software renderer, PS5 video/audio/pad drivers; statically linked, unmodified |
| stb_truetype (nothings/stb) | v1.26, `6e9f34d` | MIT or public domain (choice) | Glyph rasterisation |
| stb_image (nothings/stb) | v2.29, `6e9f34d` | MIT or public domain (choice) | JPEG and PNG decoding of profile pictures, banners and game artwork (Native 0.2+); compiled with `STBI_ONLY_JPEG`/`STBI_ONLY_PNG`, size-capped and fuzzed (`tests/fuzz`) |
| Inter | 4.1 (Regular, Bold) | SIL Open Font License 1.1 | UI font, embedded in the ELF; the OFL text ships in the artifact (`LICENSE-Inter-OFL.txt`) |
| Lucide icons (path data of crown, shield-check, braces, badge-check, sprout, flask-conical, gamepad-2, ticket) | lucide-react 1.56.0 | ISC | Badge medallions (`src/core/badges.c`, Native 0.2+), the same eight icons as the console page; the ISC notice is in the source and ships in `LICENSES.md` of every release package |
| mbedTLS, CA bundle, ps5-payload-sdk | as for the daemon (above) | Apache-2.0, MPL-2.0, GPLv3+ | TLS, trust roots, runtime and libc |

As with the daemon, the distributed PS5 ELF links the SDK's GPLv3+ runtime, so the ELF is a
combined work under the GPLv3; zlib, MIT/public-domain, Apache-2.0 and the OFL (for the embedded
font, "bundled with software") are compatible with that. The complete corresponding source is this
repository plus the pinned inputs named above.

**What a release package must carry** (checked by the packaging step of `ps5-native.yml`): this
file (`LICENSES.md`, with the Lucide ISC notice above), the Inter OFL text, the GPLv3 text and a
pointer to the corresponding source (the tagged commit of this repository, the pinned SDK commit,
SDL2 commit and the checksummed inputs), and the Apache-2.0 text for mbedTLS. stb_truetype and
stb_image are used under their MIT option (notice in `LICENSES.md`); SDL2's zlib licence asks for
no notice in binaries, which is given anyway. The MPL-2.0 CA bundle is unmodified; its source is
named by version and checksum in `BUILDINFO.txt`.

## AKENO In-Game Companion (`clients/ps5-ingame`, EXPERIMENTAL)

AKENO's sources are MIT. The plugin ELF (`AKEN00001.elf`) links, unmodified:

| Component | Version | License | Use |
| --- | --- | --- | --- |
| OnionHEN plugin SDK runtime (OnionBuddies/onionHEN-plugin-sdk) | `72527c3` | GPLv3 | Plugin descriptor, IPC client, `onion.ui` document builder; `tools/inspect_plugin.py` only reads the built ELF |
| ps5-payload-sdk | as above | GPLv3+ | Runtime and libc |
| AKENO Native's parsers and IPC client, stb_image | as above | MIT | Reading the daemon's answers |

The plugin ELF is therefore a **GPLv3** work. It ships only in the `experimental/` folder of a
release package, with this file, the GPLv3 text and the source pointer (tagged commit, SDK commits).
It requires OnionHEN (aydencharles/onionHEN, GPLv3), which AKENO does not ship or install.

### stb_truetype and stb_image (used under the MIT alternative)

```
Copyright (c) 2017 Sean Barrett

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

Read for the native architecture (2026-10-17), **no code copied**, nothing linked:

| Project | Commit | License | What was learned |
| --- | --- | --- | --- |
| TheCriicom/omega-dashboard | `4a7ac76` | GPLv3+ | Native SDL2 dashboard launched as websrv homebrew; software renderer; background daemon whose audio is silent ("no audio session") while the foreground UI's is heard; OnionHEN plugin page |
| ps5-payload-dev/websrv | `7748df9` | GPLv3+ | `hbldr`: how websrv-format homebrew becomes the foreground app (FAKE00000, `/system_ex`, kill, ptrace); `homebrew.js` format |
| Rufidj/Nativehbl | `e948810` | GPLv3 | Native launcher UI that relies on the same bigapp path and the installed launcher PKG |
| aydencharles/onionHEN, OnionBuddies/onionHEN-plugin-sdk, -boilerplate | `b23ffe6`, `72527c3`, `ae52b39` | GPLv3 | Plugin model (separate processes, versioned IPC, Toolbox UI), kstuff detection, ports, system changes |
| etaHEN/etaHEN, GronedWaffel/etahen-11.00-13.60 | `dafa13b`, `89123ff` | GPLv3 | Toolbox, plugin containers, firmware profiles (12.20 listed), "do not load a second kstuff" |
| ps5-payload-dev/SDL drivers (also linked, see above) | `ee4c47d` | zlib | Pad mapping, audio and video call sequences; the `connected` flag SDL ignores |

An OnionHEN plugin (Phase 4) would link the plugin SDK's GPLv3 runtime and would therefore be a
separate GPLv3 component; it is not started.

## Runtime dependencies (shipped)

Server (`apps/server`, installed in the Docker image):

| Package | Version | License |
| --- | --- | --- |
| fastify | 5.12.7 | MIT |
| @fastify/cookie, @fastify/helmet, @fastify/rate-limit, @fastify/static, @fastify/websocket | 11.1.3, 13.1.2, 11.2.1, 10.1.6, 11.3.4 | MIT |
| @node-rs/argon2 | 2.2.2 | MIT |
| drizzle-orm | 0.45.4 | Apache-2.0 |
| pg (node-postgres) | 8.23.1 | MIT |
| pino | 10.4.0 | MIT |
| zod | 4.6.7 | MIT |
| ws (via @fastify/websocket) | 8.x | MIT |
| sharp (image processing, Alpha 0.3) | 0.35.4 | Apache-2.0 |
| libvips, prebuilt by sharp (`@img/sharp-libvips-linuxmusl-x64` in the image) | 8.18.6 | **LGPL-3.0-or-later**, a separate shared library loaded at run time (not modified; can be replaced) |
| livekit-server-sdk (voice tokens, room API, webhooks, Alpha 0.4) | 2.19.1 | Apache-2.0 (with `@livekit/protocol` Apache-2.0, `jose` MIT, `@bufbuild/protobuf` Apache-2.0 AND BSD-3-Clause) |

Web client (`apps/web`, bundled into static files):

| Package | Version | License |
| --- | --- | --- |
| react, react-dom | 19.3.0 | MIT |
| react-router | 7.18.4 | MIT |
| @tanstack/react-query | 5.104.2 | MIT |
| lucide-react (icons) | 1.56.0 | ISC |
| @fontsource-variable/inter (Inter typeface) | 5.3.0 | SIL Open Font License 1.1 |
| @fontsource-variable/space-grotesk (Space Grotesk typeface) | 5.3.0 | SIL Open Font License 1.1 |
| zod (shared validation) | 4.6.7 | MIT |
| livekit-client (voice, loaded only when joining voice, Alpha 0.4) | 2.22.3 | Apache-2.0 (with `webrtc-adapter` BSD-3-Clause, `sdp-transform`/`sdp`/`loglevel`/`events`/`typed-emitter` MIT, `tslib` 0BSD, `machina` dual MIT / GPL-2.0, **used under MIT**) |

Infrastructure images: Node.js (MIT, with bundled third-party licenses), PostgreSQL
(PostgreSQL License), Caddy (Apache-2.0), Alpine Linux base packages (various OSI licenses),
and for the optional `voice` profile the LiveKit server image `livekit/livekit-server:v1.12.0`
(Apache-2.0), run unmodified.

## Development dependencies (not shipped)

TypeScript (Apache-2.0), Vite, Vitest, Tailwind CSS, esbuild, tsx, ESLint, typescript-eslint,
drizzle-kit, Testing Library, jsdom, pino-pretty (MIT), Playwright (Apache-2.0).

A scan of the complete installed dependency tree on 2026-10-13 found licenses MIT (316
packages), Apache-2.0 (24), ISC (20), BSD-2-Clause (8), BSD-3-Clause (8), MPL-2.0 (6),
BlueOak-1.0.0 (5), MIT-0 (2), OFL-1.1 (2), CC-BY-4.0 (1) and CC0-1.0 (1). **No GPL, LGPL or
AGPL packages are present.** The MPL-2.0 packages are `lightningcss` and its platform
binaries, used only at build time by Tailwind CSS/Vite; they are not distributed in the
runtime image or bundle. CC-BY-4.0 applies to a data package (`caniuse-lite`) used at build
time.

A production-only scan on 2026-10-14 (`pnpm licenses list --prod`, after adding sharp and the
LiveKit SDKs) found MIT (104), Apache-2.0 (10), ISC (9), BlueOak-1.0.0 (5), BSD-3-Clause (4),
OFL-1.1 (2), **LGPL-3.0-or-later (2: the libvips binaries for glibc and musl)**,
Apache-2.0 AND BSD-3-Clause (1), MIT OR GPL (1: `machina`, used under MIT) and 0BSD (1). The
statement above about GPL-family packages predates Alpha 0.3; libvips is the only LGPL
component and is used as an unmodified, dynamically loaded library, which the LGPL permits;
keep its license notice (shipped in the package) when distributing the image.

Re-run the check after dependency updates, for example with `pnpm licenses list --prod`.

## Reference projects (not used as dependencies)

| Project | License | Relationship |
| --- | --- | --- |
| Omega Dashboard for PS5 | GPL-3.0-or-later | Read as an architecture reference only. Copying its code would require GPL compliance for the combined work; this project deliberately did not copy any. |
| CutTheCord Standalone | MIT | Not used. |
| Matrix Synapse | AGPL-3.0 / commercial | Not used (evaluated as an alternative). |
| LiveKit | Apache-2.0 | **Used since Alpha 0.4** as the voice SFU (official image and SDKs, listed above); no code copied. |
| mediasoup, Janus | ISC, GPL-3.0 | Evaluated for voice, not used (comparison in VOICE.md). |
| coturn | BSD-3-Clause | Not used (LiveKit has a TURN server built in; TURN is not enabled yet). |
| OpenSpy | (game-service replacement for GameSpy protocols) | Not used; not a general PS5 solution. |
| OrbisNet, Project BlueSphere | Closed source / hosted service | Not used; only public descriptions were read. |
