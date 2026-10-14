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

Because the PS5 ELF links the SDK's GPLv3+ runtime, a distributed PS5 binary is a combined work
under the GPLv3; AKENO's MIT and mbedTLS's Apache-2.0 code are compatible with that. The complete
corresponding source is this repository plus the pinned SDK and inputs named above. The PR #1
diagnostic ELF (`clients/ps5/ps5-smoke`) is built from the same SDK.

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
