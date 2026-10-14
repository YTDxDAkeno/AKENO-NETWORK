#!/usr/bin/env node
// Checks a database upgrade on a disposable copy before it is done in production.
//
//   # Data created by the deployed (older) revision, then upgraded:
//   node infrastructure/scripts/upgrade-check.mjs --old ../akeno-deployed
//   # A restored production backup (pg_dump --format=custom, e.g. from backup.sh), then upgraded:
//   node infrastructure/scripts/upgrade-check.mjs --from-dump backups/akeno-20261014T000000Z.dump [--old ../akeno-deployed]
//
// Steps: create the disposable database "akeno_upgrade_check" (never any other name), fill it
// (seeded through the old revision's API, or pg_restore of the dump), count rows, apply this
// checkout's migrations, check that every row is still there and the new columns have their
// defaults, start this revision and use it (sign-in, conversations, device token), and with --old
// start the old revision again on the upgraded database (rollback compatibility). The database is
// dropped at the end unless --keep is given. Production is never touched.
//
// Requires: `pnpm build` in this checkout and in --old; PostgreSQL superuser URL in
// UPGRADE_DATABASE_ADMIN_URL (default postgres://akeno:akeno_dev_password@127.0.0.1:5432/postgres);
// pg_restore on PATH for --from-dump.
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { createRequire } from 'node:module';
import net from 'node:net';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const pg = createRequire(resolve(root, 'apps/server/package.json'))('pg');
const args = process.argv.slice(2);
const option = (name) => {
  const index = args.indexOf(name);
  return index >= 0 ? args[index + 1] : undefined;
};
const oldRoot = option('--old') ? resolve(option('--old')) : null;
const dump = option('--from-dump') ? resolve(option('--from-dump')) : null;
const keep = args.includes('--keep');
const adminUrl = process.env.UPGRADE_DATABASE_ADMIN_URL ?? 'postgres://akeno:akeno_dev_password@127.0.0.1:5432/postgres';
const DATABASE = 'akeno_upgrade_check';
const PASSWORD = 'upgrade-check-password-1';
const ADMIN_PASSWORD = 'upgrade-check-admin-password-1';

if (!oldRoot && !dump) {
  console.error('Usage: upgrade-check.mjs --old <older checkout> | --from-dump <backup.dump> [--old <older checkout>] [--keep]');
  process.exit(2);
}
for (const dir of [root, oldRoot].filter(Boolean)) {
  if (!existsSync(resolve(dir, 'apps/server/dist/main.js'))) throw new Error(`Run "pnpm build" in ${dir} first.`);
}

const databaseUrl = new URL(adminUrl);
databaseUrl.pathname = `/${DATABASE}`;
const sleep = (ms) => new Promise((done) => setTimeout(done, ms));
const log = (text) => process.stdout.write(`${text}\n`);

async function admin(sql) {
  const client = new pg.Client({ connectionString: adminUrl });
  await client.connect();
  try {
    await client.query(sql);
  } finally {
    await client.end();
  }
}

async function query(sql) {
  const client = new pg.Client({ connectionString: databaseUrl.toString() });
  await client.connect();
  try {
    return (await client.query(sql)).rows;
  } finally {
    await client.end();
  }
}

function freePort() {
  return new Promise((done, fail) => {
    const server = net.createServer();
    server.listen(0, '127.0.0.1', () => {
      const { port } = server.address();
      server.close(() => done(port));
    });
    server.on('error', fail);
  });
}

function env(port) {
  return {
    ...process.env,
    NODE_ENV: 'production',
    HOST: '127.0.0.1',
    PORT: String(port),
    DATABASE_URL: databaseUrl.toString(),
    PUBLIC_ORIGIN: `http://localhost:${port}`,
    REGISTRATION_MODE: 'invite',
    MIGRATE_ON_START: 'false',
    LOG_LEVEL: 'warn',
    LOG_PRETTY: 'false',
  };
}

function cli(dir, cliArgs, input) {
  const result = spawnSync('node', ['apps/server/dist/cli.js', ...cliArgs], { cwd: dir, env: env(1), input, encoding: 'utf8' });
  assert.equal(result.status, 0, `${cliArgs.join(' ')} in ${dir}: ${result.stderr}`);
  return result.stdout;
}

async function startServer(dir) {
  const port = await freePort();
  const child = spawn('node', ['apps/server/dist/main.js'], { cwd: dir, env: env(port), stdio: ['ignore', 'ignore', 'pipe'] });
  let stderr = '';
  child.stderr.on('data', (chunk) => (stderr += chunk));
  for (let i = 0; i < 100; i += 1) {
    try {
      const response = await fetch(`http://127.0.0.1:${port}/health/ready`);
      if (response.ok) return { port, child, origin: `http://localhost:${port}` };
    } catch {
      // not listening yet
    }
    if (child.exitCode !== null) break;
    await sleep(100);
  }
  child.kill('SIGKILL');
  throw new Error(`The server in ${dir} did not become ready: ${stderr}`);
}

async function stopServer(server) {
  const exited = new Promise((done) => server.child.once('exit', done));
  server.child.kill('SIGTERM');
  await exited;
}

class Session {
  constructor(server) {
    this.server = server;
    this.cookie = null;
    this.csrf = null;
    this.token = null;
  }
  async request(method, path, body) {
    const headers = { Origin: this.server.origin };
    if (body !== undefined) headers['Content-Type'] = 'application/json';
    if (this.cookie) headers.Cookie = this.cookie;
    if (this.csrf && method !== 'GET') headers['X-CSRF-Token'] = this.csrf;
    if (this.token) headers.Authorization = `Bearer ${this.token}`;
    const response = await fetch(`http://127.0.0.1:${this.server.port}/api/v1${path}`, {
      method,
      headers,
      body: body === undefined ? undefined : JSON.stringify(body),
    });
    const setCookie = response.headers.get('set-cookie');
    if (setCookie) this.cookie = setCookie.split(';')[0];
    const text = await response.text();
    return { status: response.status, body: text ? JSON.parse(text) : null };
  }
  async signIn(onlineId, password) {
    const response = await this.request('POST', '/auth/login', { onlineId, password });
    assert.equal(response.status, 200, JSON.stringify(response.body));
    this.csrf = response.body.csrfToken;
    this.user = response.body.user;
    return this;
  }
}

const COUNTED = ['users', 'profiles', 'friendships', 'conversations', 'messages', 'device_credentials', 'notifications'];
async function counts() {
  const result = {};
  for (const table of COUNTED) {
    const exists = await query(`SELECT to_regclass('public.${table}') AS t`);
    if (exists[0].t) result[table] = Number((await query(`SELECT count(*)::int AS n FROM ${table}`))[0].n);
  }
  return result;
}

/** Data an older revision would have: two friends, a message, a paired console. */
async function seedWithOldRevision() {
  cli(oldRoot, ['migrate']);
  cli(oldRoot, ['create-admin', '--online-id', 'upgrade_admin', '--password-stdin'], ADMIN_PASSWORD);
  const server = await startServer(oldRoot);
  try {
    const adminSession = await new Session(server).signIn('upgrade_admin', ADMIN_PASSWORD);
    const users = [];
    for (const onlineId of ['upgrade_a', 'upgrade_b']) {
      const invitation = await adminSession.request('POST', '/admin/invitations', { maxUses: 1, expiresInHours: 1, note: 'upgrade check' });
      const session = new Session(server);
      const registered = await session.request('POST', '/auth/register', { onlineId, password: PASSWORD, invitationCode: invitation.body.code });
      assert.equal(registered.status, 201, JSON.stringify(registered.body));
      session.csrf = registered.body.csrfToken;
      session.user = registered.body.user;
      users.push(session);
    }
    const [a, b] = users;
    const request = await a.request('POST', '/friends/requests', { userId: b.user.id });
    assert.equal((await b.request('POST', `/friends/requests/${request.body.request.id}/accept`)).status, 200);
    const conversation = await a.request('POST', '/conversations', { userId: b.user.id });
    const sent = await a.request('POST', `/conversations/${conversation.body.id}/messages`, {
      body: 'Written before the upgrade',
      clientMessageId: '12345678-1234-4234-8234-123456789012',
    });
    assert.equal(sent.status, 201, JSON.stringify(sent.body));

    const console_ = new Session(server);
    const started = await console_.request('POST', '/devices/pairings', { deviceName: 'Old PS5', platform: 'PlayStation 5 (homebrew)', clientVersion: '0.1.0' });
    assert.equal(started.status, 201, JSON.stringify(started.body));
    assert.equal((await a.request('POST', '/devices/pairings/approve', { userCode: started.body.userCode })).status, 204);
    let token = null;
    for (let i = 0; i < 20 && !token; i += 1) {
      const redeemed = await console_.request('POST', '/devices/pairings/token', { deviceCode: started.body.deviceCode });
      if (redeemed.status === 200) token = redeemed.body.accessToken;
      else await sleep(Math.max(1000, (started.body.interval ?? 5) * 1000));
    }
    assert.ok(token, 'the console was paired by the old revision');
    return { conversationId: conversation.body.id, deviceToken: token };
  } finally {
    await stopServer(server);
  }
}

/** Uses a revision on the upgraded database. */
async function useRevision(dir, label, seeded) {
  const server = await startServer(dir);
  try {
    if (!seeded) return log(`  ok   ${label}: starts and reports ready on the upgraded database`);
    const a = await new Session(server).signIn('upgrade_a', PASSWORD);
    const messages = await a.request('GET', `/conversations/${seeded.conversationId}/messages`);
    assert.equal(messages.status, 200, JSON.stringify(messages.body));
    assert.ok(messages.body.messages.some((m) => m.body === 'Written before the upgrade'));
    const device = new Session(server);
    device.token = seeded.deviceToken;
    assert.equal((await device.request('GET', '/devices/current')).status, 200, 'the console paired before the upgrade stays paired');
    log(`  ok   ${label}: sign-in, the conversation and the paired console work`);
    return { a, device };
  } finally {
    await stopServer(server);
  }
}

log(`Upgrade check on the disposable database "${DATABASE}"`);
await admin(`DROP DATABASE IF EXISTS ${DATABASE} WITH (FORCE)`);
await admin(`CREATE DATABASE ${DATABASE}`);
let failed = false;
try {
  let seeded = null;
  if (dump) {
    const restored = spawnSync('pg_restore', ['--no-owner', '--no-privileges', '--exit-on-error', '-d', databaseUrl.toString(), dump], { encoding: 'utf8' });
    assert.equal(restored.status, 0, restored.stderr);
    log(`  ok   restored ${dump}`);
  } else {
    seeded = await seedWithOldRevision();
    log('  ok   seeded through the old revision (two friends, a message, a paired console)');
  }
  const before = await counts();
  log(`       rows before: ${JSON.stringify(before)}`);

  log(cli(root, ['migrate']).trim().replace(/^/gm, '       '));
  const after = await counts();
  for (const [table, n] of Object.entries(before)) assert.equal(after[table], n, `${table}: ${n} rows before, ${after[table]} after`);
  log('  ok   every counted row is still there after the migrations');
  const defaults = await query(
    `SELECT count(*) FILTER (WHERE game_activity_tracking AND game_playtime_visible AND game_excluded_titles = '{}')::int AS ok,
            count(*)::int AS total FROM profiles`,
  );
  assert.equal(defaults[0].ok, defaults[0].total, 'new privacy columns have their defaults');
  log('  ok   new columns have their defaults (tracking on, playtime visible, nothing excluded)');

  const current = await useRevision(root, 'this revision', seeded);
  if (seeded) {
    const server = await startServer(root);
    try {
      const device = new Session(server);
      device.token = seeded.deviceToken;
      const beat = await device.request('POST', '/activity/heartbeat', { titleId: 'PPSA01234', platform: 'PS5' });
      assert.equal(beat.status, 403);
      assert.equal(beat.body.error.code, 'DEVICE_SCOPE_DENIED');
      log('  ok   the old console has no game-activity permission until its owner allows it');
    } finally {
      await stopServer(server);
    }
  }
  void current;
  if (oldRoot) await useRevision(oldRoot, 'old revision on the upgraded database (rollback)', seeded);
  log('Upgrade check passed.');
} catch (error) {
  failed = true;
  log(`  FAIL ${String(error?.stack ?? error)}`);
} finally {
  if (!keep) await admin(`DROP DATABASE IF EXISTS ${DATABASE} WITH (FORCE)`);
}
process.exit(failed ? 1 : 0);
