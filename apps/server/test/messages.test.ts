import { randomUUID } from 'node:crypto';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import {
  connectRealtime,
  createTestServer,
  listen,
  login,
  makeFriends,
  registerUser,
  sleep,
  type TestClient,
  type TestServer,
} from './helpers.js';

let server: TestServer;
let url: string;

beforeAll(async () => {
  server = await createTestServer();
  url = await listen(server);
});

afterAll(async () => {
  await server.close();
});

async function friendsWithConversation(prefix: string): Promise<{ a: TestClient; b: TestClient; conversationId: string }> {
  const a = await registerUser(server, `${prefix}_a`);
  const b = await registerUser(server, `${prefix}_b`);
  await makeFriends(a, b);
  const conversation = await a.post('/conversations', { userId: b.id });
  expect(conversation.status).toBe(200);
  return { a, b, conversationId: conversation.body.id };
}

function send(client: TestClient, conversationId: string, body: string, clientMessageId = randomUUID()) {
  return client.post(`/conversations/${conversationId}/messages`, { body, clientMessageId });
}

describe('direct messages', () => {
  it('finds or creates a single conversation per pair', async () => {
    const { a, b, conversationId } = await friendsWithConversation('dm_pair');
    const again = await b.post('/conversations', { userId: a.id });
    expect(again.body.id).toBe(conversationId);
    const concurrent = await Promise.all([
      a.post('/conversations', { userId: b.id }),
      b.post('/conversations', { userId: a.id }),
    ]);
    expect(new Set(concurrent.map((response) => response.body.id))).toEqual(new Set([conversationId]));
    expect((await a.post('/conversations', { userId: a.id })).status).toBe(400);
  });

  it('persists messages and returns them in chronological order', async () => {
    const { a, b, conversationId } = await friendsWithConversation('dm_order');
    const first = await send(a, conversationId, 'Hello Bob');
    expect(first.status).toBe(201);
    expect(first.body.created).toBe(true);
    expect(first.body.message.sender.id).toBe(a.id);
    await send(b, conversationId, 'Hi Alice');
    const history = await b.get(`/conversations/${conversationId}/messages`);
    expect(history.body.messages.map((message: { body: string }) => message.body)).toEqual(['Hello Bob', 'Hi Alice']);
    expect(history.body.hasMore).toBe(false);
  });

  it('deduplicates client retries with the same client message ID', async () => {
    const { a, conversationId } = await friendsWithConversation('dm_idem');
    const clientMessageId = randomUUID();
    const original = await send(a, conversationId, 'Only once', clientMessageId);
    const retry = await send(a, conversationId, 'Only once', clientMessageId);
    const parallel = await Promise.all([1, 2, 3].map(() => send(a, conversationId, 'Only once', clientMessageId)));
    expect(retry.status).toBe(200);
    expect(retry.body.created).toBe(false);
    expect(retry.body.message.id).toBe(original.body.message.id);
    for (const response of parallel) expect(response.body.message.id).toBe(original.body.message.id);
    const history = await a.get(`/conversations/${conversationId}/messages`);
    expect(history.body.messages).toHaveLength(1);
  });

  it('paginates with before and after cursors', async () => {
    const { a, conversationId } = await friendsWithConversation('dm_page');
    for (let i = 1; i <= 7; i += 1) await send(a, conversationId, `Message ${i}`);
    const latest = await a.get(`/conversations/${conversationId}/messages?limit=3`);
    expect(latest.body.messages.map((m: { body: string }) => m.body)).toEqual(['Message 5', 'Message 6', 'Message 7']);
    expect(latest.body.hasMore).toBe(true);
    const older = await a.get(`/conversations/${conversationId}/messages?limit=3&before=${latest.body.messages[0].id}`);
    expect(older.body.messages.map((m: { body: string }) => m.body)).toEqual(['Message 2', 'Message 3', 'Message 4']);
    const oldest = await a.get(`/conversations/${conversationId}/messages?limit=3&before=${older.body.messages[0].id}`);
    expect(oldest.body.messages.map((m: { body: string }) => m.body)).toEqual(['Message 1']);
    expect(oldest.body.hasMore).toBe(false);
    const after = await a.get(`/conversations/${conversationId}/messages?after=${older.body.messages[2].id}`);
    expect(after.body.messages.map((m: { body: string }) => m.body)).toEqual(['Message 5', 'Message 6', 'Message 7']);
    expect((await a.get(`/conversations/${conversationId}/messages?before=1&after=2`)).status).toBe(400);
  });

  it('tracks unread counts and read state', async () => {
    const { a, b, conversationId } = await friendsWithConversation('dm_unread');
    await send(a, conversationId, 'One');
    const second = await send(a, conversationId, 'Two');
    const listB = await b.get('/conversations');
    expect(listB.body.conversations[0]).toMatchObject({ id: conversationId, unreadCount: 2 });
    expect(listB.body.conversations[0].lastMessage.body).toBe('Two');
    expect((await a.get('/conversations')).body.conversations[0].unreadCount).toBe(0);
    const read = await b.post(`/conversations/${conversationId}/read`, { lastReadMessageId: second.body.message.id });
    expect(read.body.lastReadMessageId).toBe(second.body.message.id);
    expect((await b.get('/conversations')).body.conversations[0].unreadCount).toBe(0);
    // Read cursors never move backwards and are clamped to existing messages.
    await b.post(`/conversations/${conversationId}/read`, { lastReadMessageId: 1 });
    const clamped = await b.post(`/conversations/${conversationId}/read`, { lastReadMessageId: 999_999_999 });
    expect(clamped.body.lastReadMessageId).toBe(second.body.message.id);
  });

  it('denies access to conversations the user is not part of', async () => {
    const { conversationId } = await friendsWithConversation('dm_private');
    const outsider = await registerUser(server, 'dm_outsider');
    expect((await outsider.get(`/conversations/${conversationId}`)).status).toBe(404);
    expect((await outsider.get(`/conversations/${conversationId}/messages`)).status).toBe(404);
    expect((await send(outsider, conversationId, 'Let me in')).status).toBe(404);
    expect((await outsider.post(`/conversations/${conversationId}/read`, { lastReadMessageId: 1 })).status).toBe(404);
    expect((await outsider.get('/conversations')).body.conversations).toHaveLength(0);
  });

  it('validates message bodies', async () => {
    const { a, conversationId } = await friendsWithConversation('dm_valid');
    expect((await send(a, conversationId, '   ')).status).toBe(400);
    expect((await send(a, conversationId, 'x'.repeat(2001))).status).toBe(400);
    expect((await a.post(`/conversations/${conversationId}/messages`, { body: 'missing id' })).status).toBe(400);
  });

  it('delivers messages in realtime to every device of both participants', async () => {
    const { a, b, conversationId } = await friendsWithConversation('dm_live');
    const bobSocket = await connectRealtime(url, b);
    const bobSecondDevice = await connectRealtime(url, await login(server, 'dm_live_b'));
    const aliceSocket = await connectRealtime(url, a);
    const sent = await send(a, conversationId, 'Realtime hello');
    for (const socket of [bobSocket, bobSecondDevice, aliceSocket]) {
      const event = await socket.waitFor('message.created', (e) => e.data.message.id === sent.body.message.id);
      expect(event.data).toMatchObject({ conversationId, conversationKind: 'direct' });
      expect(event.data.message.body).toBe('Realtime hello');
    }
    // An idempotent retry must not produce a second realtime event.
    bobSocket.clear();
    await send(a, conversationId, 'Realtime hello', sent.body.message.clientMessageId);
    await sleep(200);
    expect(bobSocket.events.filter((event) => event.type === 'message.created')).toHaveLength(0);
    await Promise.all([bobSocket.close(), bobSecondDevice.close(), aliceSocket.close()]);
  });

  it('relays typing indicators only to other participants', async () => {
    const { a, b, conversationId } = await friendsWithConversation('dm_typing');
    const outsider = await registerUser(server, 'dm_typing_x');
    const aliceSocket = await connectRealtime(url, a);
    const bobSocket = await connectRealtime(url, b);
    const outsiderSocket = await connectRealtime(url, outsider);
    aliceSocket.send({ v: 1, type: 'typing', data: { conversationId } });
    const typing = await bobSocket.waitFor('typing');
    expect(typing.data).toEqual({ conversationId, userId: a.id });
    outsiderSocket.send({ v: 1, type: 'typing', data: { conversationId } });
    await sleep(200);
    expect(aliceSocket.events.some((event) => event.type === 'typing')).toBe(false);
    await Promise.all([aliceSocket.close(), bobSocket.close(), outsiderSocket.close()]);
  });

  it('recovers missed messages after a reconnect using the after cursor', async () => {
    const { a, b, conversationId } = await friendsWithConversation('dm_recover');
    const bobSocket = await connectRealtime(url, b);
    const first = await send(a, conversationId, 'Before disconnect');
    await bobSocket.waitFor('message.created');
    await bobSocket.close();
    await send(a, conversationId, 'Missed one');
    await send(a, conversationId, 'Missed two');
    const reconnected = await connectRealtime(url, b);
    const recovered = await b.get(`/conversations/${conversationId}/messages?after=${first.body.message.id}`);
    expect(recovered.body.messages.map((m: { body: string }) => m.body)).toEqual(['Missed one', 'Missed two']);
    await reconnected.close();
  });
});

describe('persistence', () => {
  it('keeps messages, friendships and sessions across a server restart', async () => {
    const isolated = await createTestServer();
    const a = await registerUser(isolated, 'persist_a');
    const b = await registerUser(isolated, 'persist_b');
    await makeFriends(a, b);
    const conversation = await a.post('/conversations', { userId: b.id });
    await a.post(`/conversations/${conversation.body.id}/messages`, { body: 'Survives restarts', clientMessageId: randomUUID() });

    const restarted = await isolated.restart();
    try {
      // Re-bind the existing cookies to the new app instance.
      const { TestClient } = await import('./helpers.js');
      const bAfter = new TestClient(restarted.app);
      bAfter.cookie = b.cookie;
      bAfter.csrf = b.csrf;
      const history = await bAfter.get(`/conversations/${conversation.body.id}/messages`);
      expect(history.status).toBe(200);
      expect(history.body.messages[0].body).toBe('Survives restarts');
      expect((await bAfter.get('/friends')).body.friends[0].id).toBe(a.id);
    } finally {
      await restarted.close();
    }
  });
});
