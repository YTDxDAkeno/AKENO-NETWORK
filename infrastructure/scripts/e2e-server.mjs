#!/usr/bin/env node
// Starts a disposable AKENO NETWORK instance for browser end-to-end tests:
//   1. drops and recreates the E2E database (never point this at real data),
//   2. bootstraps an administrator through the real CLI,
//   3. runs the production server build, which serves the production web build.
// Requires `pnpm build` beforehand.
import { spawn, spawnSync } from 'node:child_process';
import { existsSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { createRequire } from 'node:module';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
// pg is a dev dependency of the web package (used only by this test tooling).
const pg = createRequire(resolve(root, 'apps/web/package.json'))('pg');
const adminUrl = process.env.E2E_DATABASE_ADMIN_URL ?? 'postgres://akeno:akeno_dev_password@127.0.0.1:5432/postgres';
const databaseName = process.env.E2E_DATABASE_NAME ?? 'akeno_e2e';
const port = process.env.E2E_PORT ?? '3200';
const adminPassword = process.env.E2E_ADMIN_PASSWORD ?? 'e2e-admin-password-123';

if (!/^akeno_e2e[a-z0-9_]*$/.test(databaseName)) {
  console.error('Refusing to reset a database whose name does not start with "akeno_e2e".');
  process.exit(1);
}
for (const required of ['apps/server/dist/main.js', 'apps/server/dist/cli.js', 'apps/web/dist/index.html']) {
  if (!existsSync(resolve(root, required))) {
    console.error(`Missing ${required}. Run "pnpm build" first.`);
    process.exit(1);
  }
}

const client = new pg.Client({ connectionString: adminUrl });
await client.connect();
await client.query(`DROP DATABASE IF EXISTS ${databaseName} WITH (FORCE)`);
await client.query(`CREATE DATABASE ${databaseName}`);
await client.end();

// Uploaded images belong to the disposable database: start with an empty media directory too.
const mediaDir = process.env.E2E_MEDIA_DIR ?? join(tmpdir(), `akeno-e2e-media-${databaseName}`);
rmSync(mediaDir, { recursive: true, force: true });

const databaseUrl = new URL(adminUrl);
databaseUrl.pathname = `/${databaseName}`;
const env = {
  ...process.env,
  NODE_ENV: 'production',
  HOST: '127.0.0.1',
  PORT: port,
  DATABASE_URL: databaseUrl.toString(),
  PUBLIC_ORIGIN: `http://localhost:${port}`,
  REGISTRATION_MODE: 'invite',
  WEB_DIST_DIR: resolve(root, 'apps/web/dist'),
  MEDIA_DIR: mediaDir,
  MIGRATE_ON_START: 'true',
  // Rate limiting stays on (per-route limits such as registration behave as in production), but
  // the general per-user budget is raised: the specs reuse the same few accounts back to back,
  // far faster than a person, and the voice spec alone makes hundreds of requests a minute.
  RATE_LIMIT_MAX_PER_MINUTE: process.env.E2E_RATE_LIMIT_MAX_PER_MINUTE ?? '1200',
  // Voice is off unless a LiveKit server is given for the voice spec (never inherited by accident).
  LIVEKIT_URL: process.env.E2E_LIVEKIT_URL ?? '',
  LIVEKIT_API_URL: process.env.E2E_LIVEKIT_API_URL ?? '',
  LIVEKIT_API_KEY: process.env.E2E_LIVEKIT_API_KEY ?? '',
  LIVEKIT_API_SECRET: process.env.E2E_LIVEKIT_API_SECRET ?? '',
  // SharePlay (experimental) rides on the same LiveKit server: on exactly when voice is.
  SHAREPLAY_ENABLED: process.env.E2E_LIVEKIT_URL ? 'true' : 'false',
  SHAREPLAY_REMOTE_INPUT_ENABLED: process.env.E2E_LIVEKIT_URL ? 'true' : 'false',
  LOG_LEVEL: process.env.E2E_LOG_LEVEL ?? 'warn',
  LOG_PRETTY: 'false',
};

const migrate = spawnSync('node', ['apps/server/dist/cli.js', 'migrate'], { cwd: root, env, stdio: 'inherit' });
if (migrate.status !== 0) process.exit(migrate.status ?? 1);
const admin = spawnSync('node', ['apps/server/dist/cli.js', 'create-admin', '--online-id', 'e2e_admin', '--password-stdin'], {
  cwd: root,
  env,
  input: adminPassword,
  stdio: ['pipe', 'inherit', 'inherit'],
});
if (admin.status !== 0) process.exit(admin.status ?? 1);

const server = spawn('node', ['apps/server/dist/main.js'], { cwd: root, env, stdio: 'inherit' });
const stop = () => server.kill('SIGTERM');
process.on('SIGTERM', stop);
process.on('SIGINT', stop);
server.on('exit', (code) => process.exit(code ?? 0));
