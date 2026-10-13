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

## Device client (`clients/ps5`)

All C sources (JSON, HTTP, WebSocket, SHA-1/Base64, client, CLI, tests) were written for this
project; no third-party source is vendored. The Linux build links the system **OpenSSL 3**
library (Apache-2.0) dynamically. A console build would need a TLS library for that target;
its license must be recorded here before it is added.

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

Infrastructure images: Node.js (MIT, with bundled third-party licenses), PostgreSQL
(PostgreSQL License), Caddy (Apache-2.0), Alpine Linux base packages (various OSI licenses).

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

Re-run the check after dependency updates, for example with `pnpm licenses list --prod`.

## Reference projects (not used as dependencies)

| Project | License | Relationship |
| --- | --- | --- |
| Omega Dashboard for PS5 | GPL-3.0-or-later | Read as an architecture reference only. Copying its code would require GPL compliance for the combined work; this project deliberately did not copy any. |
| CutTheCord Standalone | MIT | Not used. |
| Matrix Synapse | AGPL-3.0 / commercial | Not used (evaluated as an alternative). |
| LiveKit | Apache-2.0 | Candidate SFU for a future voice phase; not used yet. |
| coturn | BSD-3-Clause | Candidate TURN server for a future voice phase; not used yet. |
| OpenSpy | (game-service replacement for GameSpy protocols) | Not used; not a general PS5 solution. |
| OrbisNet, Project BlueSphere | Closed source / hosted service | Not used; only public descriptions were read. |
