#!/usr/bin/env node
/**
 * AKENO NETWORK lightweight load test.
 *
 * Simulates N authenticated users against a running instance and measures:
 *   registration, login, profile retrieval, friend list retrieval, WebSocket connection,
 *   direct-message delivery latency (REST send -> WebSocket receive), party update latency,
 *   and server memory / connections / database pool (via the admin stats endpoint).
 *
 * Run it ONLY against a disposable test instance. It creates accounts and data.
 * The target should run with RATE_LIMIT_ENABLED=false, because all simulated users share
 * one IP address and would otherwise (correctly) be rate limited.
 *
 * Usage:
 *   AKENO_URL=http://localhost:3300 LOAD_ADMIN_ONLINE_ID=admin LOAD_ADMIN_PASSWORD=... \
 *   node infrastructure/scripts/load-test.mjs --users 20 --messages 200
 *
 * Options: --users N (default 20), --messages N (default 100), --concurrency N (default 5),
 *          --server-pid PID (optional, samples CPU/RSS with ps), --json PATH (write results)
 */
import { execFileSync } from 'node:child_process';
import { randomUUID } from 'node:crypto';
import { writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, resolve } from 'node:path';
import { parseArgs } from 'node:util';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const WebSocket = createRequire(resolve(root, 'apps/server/package.json'))('ws');

const { values } = parseArgs({
  options: {
    users: { type: 'string', default: '20' },
    messages: { type: 'string', default: '100' },
    concurrency: { type: 'string', default: '5' },
    'server-pid': { type: 'string' },
    json: { type: 'string' },
  },
});

const BASE = (process.env.AKENO_URL ?? 'http://localhost:3300').replace(/\/$/, '');
const ORIGIN = new URL(BASE).origin;
const USERS = Number(values.users);
const MESSAGES = Number(values.messages);
const CONCURRENCY = Number(values.concurrency);
const PASSWORD = 'load-test-password-123';
const RUN = Date.now().toString(36).slice(-5);

// ---------------------------------------------------------------------------------------------
// Helpers

class Client {
  constructor(label) {
    this.label = label;
    this.cookie = null;
    this.csrf = null;
    this.user = null;
  }

  async request(method, path, body) {
    const headers = { Accept: 'application/json', Origin: ORIGIN };
    if (body !== undefined) headers['Content-Type'] = 'application/json';
    if (this.cookie) headers.Cookie = this.cookie;
    if (this.csrf && method !== 'GET') headers['X-CSRF-Token'] = this.csrf;
    const started = performance.now();
    const response = await fetch(`${BASE}/api/v1${path}`, {
      method,
      headers,
      body: body === undefined ? undefined : JSON.stringify(body),
    });
    const elapsed = performance.now() - started;
    const setCookie = response.headers.get('set-cookie');
    if (setCookie) this.cookie = setCookie.split(';')[0];
    const text = await response.text();
    const data = text ? JSON.parse(text) : null;
    if (!response.ok) {
      throw new Error(`${method} ${path} -> ${response.status} ${data?.error?.code ?? ''} ${data?.error?.message ?? ''}`);
    }
    return { data, elapsed };
  }

  adopt(session) {
    this.csrf = session.csrfToken;
    this.user = session.user;
  }
}

function percentile(samples, p) {
  if (samples.length === 0) return 0;
  const sorted = [...samples].sort((a, b) => a - b);
  const index = Math.min(sorted.length - 1, Math.ceil((p / 100) * sorted.length) - 1);
  return sorted[Math.max(0, index)];
}

function summarize(samples) {
  const round = (value) => Math.round(value * 10) / 10;
  return {
    count: samples.length,
    p50: round(percentile(samples, 50)),
    p95: round(percentile(samples, 95)),
    max: round(Math.max(0, ...samples)),
    mean: round(samples.reduce((sum, value) => sum + value, 0) / Math.max(1, samples.length)),
  };
}

async function inBatches(items, size, task) {
  const results = [];
  for (let index = 0; index < items.length; index += size) {
    results.push(...(await Promise.all(items.slice(index, index + size).map(task))));
  }
  return results;
}

function connect(client) {
  return new Promise((resolvePromise, reject) => {
    const started = performance.now();
    const url = `${BASE.replace(/^http/, 'ws')}/ws`;
    const socket = new WebSocket(url, { headers: { Origin: ORIGIN, Cookie: client.cookie } });
    const listeners = new Set();
    socket.on('message', (raw) => {
      const event = JSON.parse(raw.toString());
      if (event.type === 'hello') resolvePromise({ socket, listeners, elapsed: performance.now() - started });
      for (const listener of listeners) listener(event);
    });
    socket.once('error', reject);
    socket.once('unexpected-response', (_request, response) => reject(new Error(`WebSocket rejected: HTTP ${response.statusCode}`)));
  });
}

function waitForEvent(connection, predicate, timeoutMs = 10_000) {
  return new Promise((resolvePromise, reject) => {
    const timer = setTimeout(() => {
      connection.listeners.delete(listener);
      reject(new Error('Timed out waiting for realtime event'));
    }, timeoutMs);
    const listener = (event) => {
      if (!predicate(event)) return;
      clearTimeout(timer);
      connection.listeners.delete(listener);
      resolvePromise(event);
    };
    connection.listeners.add(listener);
  });
}

function sampleProcess(pid) {
  if (!pid) return null;
  try {
    const [cpu, rss] = execFileSync('ps', ['-o', '%cpu=,rss=', '-p', pid], { encoding: 'utf8' }).trim().split(/\s+/);
    return { cpuPercent: Number(cpu), rssMb: Math.round((Number(rss) / 1024) * 10) / 10 };
  } catch {
    return null;
  }
}

// ---------------------------------------------------------------------------------------------
// Scenario

const results = {
  target: BASE,
  startedAt: new Date().toISOString(),
  parameters: { users: USERS, messages: MESSAGES, concurrency: CONCURRENCY },
  environment: { node: process.version, platform: `${process.platform} ${process.arch}` },
  measurements: {},
  server: {},
};

const admin = new Client('admin');
let adminAvailable = false;
if (process.env.LOAD_ADMIN_ONLINE_ID && process.env.LOAD_ADMIN_PASSWORD) {
  const login = await admin.request('POST', '/auth/login', {
    onlineId: process.env.LOAD_ADMIN_ONLINE_ID,
    password: process.env.LOAD_ADMIN_PASSWORD,
  });
  admin.adopt(login.data);
  adminAvailable = true;
  results.server.before = (await admin.request('GET', '/admin/stats')).data;
}
results.server.processBefore = sampleProcess(values['server-pid']);

let invitationCode;
if (adminAvailable) {
  const created = await admin.request('POST', '/admin/invitations', {
    maxUses: Math.min(100, USERS),
    expiresInHours: 1,
    note: `Load test ${RUN}`,
  });
  invitationCode = created.data.code;
}

console.log(`Registering ${USERS} users against ${BASE} …`);
const clients = Array.from({ length: USERS }, (_, index) => new Client(`lt${RUN}_${index}`));
const registerTimes = await inBatches(clients, CONCURRENCY, async (client) => {
  const { data, elapsed } = await client.request('POST', '/auth/register', {
    onlineId: client.label,
    password: PASSWORD,
    ...(invitationCode ? { invitationCode } : {}),
  });
  client.adopt(data);
  return elapsed;
});
results.measurements.registrationMs = summarize(registerTimes);

console.log('Logging in …');
const loginTimes = await inBatches(clients, CONCURRENCY, async (client) => {
  const fresh = new Client(client.label);
  const { data, elapsed } = await fresh.request('POST', '/auth/login', { onlineId: client.label, password: PASSWORD });
  client.cookie = fresh.cookie;
  client.adopt(data);
  return elapsed;
});
results.measurements.loginMs = summarize(loginTimes);

console.log('Creating friendships (each user befriends the next one) …');
const pairs = clients.map((client, index) => [client, clients[(index + 1) % clients.length]]).slice(0, USERS > 2 ? USERS : 1);
await inBatches(pairs, CONCURRENCY, async ([a, b]) => {
  const sent = await a.request('POST', '/friends/requests', { userId: b.user.id });
  if (sent.data.status === 'pending') await b.request('POST', `/friends/requests/${sent.data.request.id}/accept`);
});

console.log('Connecting WebSockets …');
const connections = await inBatches(clients, CONCURRENCY, (client) => connect(client));
results.measurements.websocketConnectMs = summarize(connections.map((connection) => connection.elapsed));
await new Promise((resolvePromise) => setTimeout(resolvePromise, 500));

console.log('Measuring profile and friend list retrieval …');
const profileTimes = await inBatches(clients, CONCURRENCY, async (client) => {
  const other = clients[(clients.indexOf(client) + 1) % clients.length];
  return (await client.request('GET', `/users/${other.label}`)).elapsed;
});
results.measurements.profileRetrievalMs = summarize(profileTimes);
const friendListTimes = await inBatches(clients, CONCURRENCY, async (client) => (await client.request('GET', '/friends')).elapsed);
results.measurements.friendListMs = summarize(friendListTimes);

console.log(`Sending ${MESSAGES} direct messages and timing realtime delivery …`);
const conversations = await inBatches(clients, CONCURRENCY, async (client) => {
  const friend = clients[(clients.indexOf(client) + 1) % clients.length];
  return (await client.request('POST', '/conversations', { userId: friend.user.id })).data.id;
});
const sendTimes = [];
const deliveryTimes = [];
const messageJobs = Array.from({ length: MESSAGES }, (_, index) => index);
await inBatches(messageJobs, CONCURRENCY, async (index) => {
  const senderIndex = index % clients.length;
  const recipientIndex = (senderIndex + 1) % clients.length;
  const clientMessageId = randomUUID();
  const started = performance.now();
  const delivered = waitForEvent(
    connections[recipientIndex],
    (event) => event.type === 'message.created' && event.data.message.clientMessageId === clientMessageId,
  );
  const { elapsed } = await clients[senderIndex].request('POST', `/conversations/${conversations[senderIndex]}/messages`, {
    body: `Load test message ${index}`,
    clientMessageId,
  });
  sendTimes.push(elapsed);
  await delivered;
  deliveryTimes.push(performance.now() - started);
});
results.measurements.messageSendAckMs = summarize(sendTimes);
results.measurements.messageDeliveryMs = summarize(deliveryTimes);

console.log('Measuring party updates …');
const leader = clients[0];
const partyMembers = [clients[1], clients[clients.length - 1]].filter((client, index, all) => client && client !== leader && all.indexOf(client) === index);
const party = (await leader.request('POST', '/parties', { privacy: 'friends', maxMembers: 16 })).data;
const partyTimes = [];
for (const member of partyMembers) {
  const started = performance.now();
  const update = waitForEvent(connections[0], (event) => event.type === 'party.changed' && event.data.partyId === party.id);
  await member.request('POST', `/parties/${party.id}/join`);
  await update;
  partyTimes.push(performance.now() - started);
}
results.measurements.partyJoinUpdateMs = summarize(partyTimes);

console.log('Measuring game session joins …');
const host = clients[0];
const maxParticipants = Math.min(64, Math.max(2, USERS));
const session = (
  await host.request('POST', '/game-sessions', { gameName: 'Load Test Arena', visibility: 'public', maxParticipants })
).data;
const sessionJoinTimes = await inBatches(clients.slice(1, maxParticipants), CONCURRENCY, async (client) =>
  (await client.request('POST', `/game-sessions/${session.id}/join`, {})).elapsed,
);
results.measurements.gameSessionJoinMs = summarize(sessionJoinTimes);

if (adminAvailable) results.server.after = (await admin.request('GET', '/admin/stats')).data;
results.server.processAfter = sampleProcess(values['server-pid']);

for (const connection of connections) connection.socket.close();
results.finishedAt = new Date().toISOString();

console.log('\nResults (milliseconds, measured from this client):');
console.table(results.measurements);
if (results.server.after) {
  const after = results.server.after;
  console.log(
    `Server after test: RSS ${(after.process.rssBytes / 1048576).toFixed(1)} MB, heap ${(after.process.heapUsedBytes / 1048576).toFixed(1)} MB, ` +
      `${after.realtime.connections} WebSocket connections, DB pool ${after.database.totalConnections} (${after.database.idleConnections} idle).`,
  );
}
if (results.server.processAfter) console.log('Server process sample (ps):', results.server.processAfter);
if (values.json) {
  writeFileSync(values.json, JSON.stringify(results, null, 2));
  console.log(`Wrote ${values.json}`);
}
