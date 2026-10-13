import { and, eq, or } from 'drizzle-orm';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { friendRequests, friendships } from '../src/database/schema/index.js';
import { createTestServer, makeFriends, registerUser, type TestServer } from './helpers.js';

let server: TestServer;

beforeAll(async () => {
  server = await createTestServer();
});

afterAll(async () => {
  await server.close();
});

describe('friend requests', () => {
  it('sends, receives and accepts a friend request', async () => {
    const alice = await registerUser(server, 'fr_alice');
    const bob = await registerUser(server, 'fr_bob');
    const search = await alice.get('/users/search?q=fr_b');
    expect(search.body.users.map((user: { onlineId: string }) => user.onlineId)).toEqual(['fr_bob']);
    expect(search.body.users[0].relationship).toBe('none');

    const sent = await alice.post('/friends/requests', { userId: bob.id });
    expect(sent.status).toBe(201);
    expect(sent.body.status).toBe('pending');

    const incoming = await bob.get('/friends/requests');
    expect(incoming.body.incoming).toHaveLength(1);
    expect(incoming.body.incoming[0].user.onlineId).toBe('fr_alice');
    expect((await alice.get('/friends/requests')).body.outgoing).toHaveLength(1);

    const notifications = await bob.get('/notifications');
    expect(notifications.body.notifications[0].data.type).toBe('friend_request.received');

    expect((await bob.post(`/friends/requests/${sent.body.request.id}/accept`)).status).toBe(200);
    const aliceFriends = await alice.get('/friends');
    expect(aliceFriends.body.friends.map((friend: { id: string }) => friend.id)).toEqual([bob.id]);
    const bobFriends = await bob.get('/friends');
    expect(bobFriends.body.friends.map((friend: { id: string }) => friend.id)).toEqual([alice.id]);
    expect((await alice.get('/users/fr_bob')).body.relationship.state).toBe('friend');
  });

  it('rejects self requests, duplicates and requests to existing friends', async () => {
    const a = await registerUser(server, 'dup_a');
    const b = await registerUser(server, 'dup_b');
    expect((await a.post('/friends/requests', { userId: a.id })).status).toBe(400);
    expect((await a.post('/friends/requests', { userId: b.id })).status).toBe(201);
    const again = await a.post('/friends/requests', { userId: b.id });
    expect(again.status).toBe(409);
    expect(again.body.error.code).toBe('REQUEST_PENDING');
    const request = (await b.get('/friends/requests')).body.incoming[0];
    await b.post(`/friends/requests/${request.id}/accept`);
    const already = await a.post('/friends/requests', { userId: b.id });
    expect(already.body.error.code).toBe('ALREADY_FRIENDS');
  });

  it('turns a reverse request into an acceptance', async () => {
    const a = await registerUser(server, 'rev_a');
    const b = await registerUser(server, 'rev_b');
    await a.post('/friends/requests', { userId: b.id });
    const reverse = await b.post('/friends/requests', { userId: a.id });
    expect(reverse.status).toBe(200);
    expect(reverse.body.status).toBe('accepted');
    expect((await a.get('/friends')).body.friends).toHaveLength(1);
  });

  it('produces exactly one friendship when both users request each other simultaneously', async () => {
    const a = await registerUser(server, 'race_a');
    const b = await registerUser(server, 'race_b');
    const results = await Promise.all([
      a.post('/friends/requests', { userId: b.id }),
      b.post('/friends/requests', { userId: a.id }),
    ]);
    for (const result of results) expect([200, 201]).toContain(result.status);
    const rows = await server.ctx.db
      .select()
      .from(friendships)
      .where(or(eq(friendships.userLowId, a.id), eq(friendships.userHighId, a.id)));
    expect(rows).toHaveLength(1);
    const pending = await server.ctx.db
      .select()
      .from(friendRequests)
      .where(and(eq(friendRequests.status, 'pending'), or(eq(friendRequests.senderId, a.id), eq(friendRequests.recipientId, a.id))));
    expect(pending).toHaveLength(0);
  });

  it('only the recipient can accept or decline, only the sender can cancel', async () => {
    const sender = await registerUser(server, 'own_sender');
    const recipient = await registerUser(server, 'own_recip');
    const outsider = await registerUser(server, 'own_outsider');
    const sent = await sender.post('/friends/requests', { userId: recipient.id });
    const id = sent.body.request.id;
    expect((await outsider.post(`/friends/requests/${id}/accept`)).status).toBe(404);
    expect((await sender.post(`/friends/requests/${id}/accept`)).status).toBe(404);
    expect((await outsider.post(`/friends/requests/${id}/cancel`)).status).toBe(404);
    expect((await recipient.post(`/friends/requests/${id}/cancel`)).status).toBe(404);
    expect((await recipient.post(`/friends/requests/${id}/decline`)).status).toBe(200);
    expect((await recipient.post(`/friends/requests/${id}/accept`)).status).toBe(409);

    const second = await sender.post('/friends/requests', { userId: recipient.id });
    expect(second.status).toBe(201);
    expect((await sender.post(`/friends/requests/${second.body.request.id}/cancel`)).status).toBe(200);
    expect((await recipient.get('/friends/requests')).body.incoming).toHaveLength(0);
  });

  it('removes friends', async () => {
    const a = await registerUser(server, 'rm_a');
    const b = await registerUser(server, 'rm_b');
    await makeFriends(a, b);
    expect((await a.delete(`/friends/${b.id}`)).status).toBe(204);
    expect((await b.get('/friends')).body.friends).toHaveLength(0);
    expect((await a.delete(`/friends/${b.id}`)).status).toBe(404);
  });

  it('respects the "nobody" friend request privacy setting', async () => {
    const a = await registerUser(server, 'priv_a');
    const b = await registerUser(server, 'priv_b');
    await b.patch('/profile/privacy', { friendRequestPrivacy: 'nobody' });
    const response = await a.post('/friends/requests', { userId: b.id });
    expect(response.status).toBe(403);
    expect(response.body.error.code).toBe('PRIVACY_RESTRICTED');
    expect((await a.get('/users/priv_b')).body.relationship.canSendFriendRequest).toBe(false);
  });
});

describe('blocking', () => {
  it('removes the friendship and prevents further interaction', async () => {
    const blocker = await registerUser(server, 'blk_blocker');
    const blocked = await registerUser(server, 'blk_blocked');
    await makeFriends(blocker, blocked);
    const conversation = await blocker.post('/conversations', { userId: blocked.id });
    expect(conversation.status).toBe(200);

    expect((await blocker.post('/blocks', { userId: blocked.id })).status).toBe(204);
    expect((await blocker.get('/friends')).body.friends).toHaveLength(0);
    expect((await blocked.get('/friends')).body.friends).toHaveLength(0);
    expect((await blocker.get('/blocks')).body.blocked[0].user.id).toBe(blocked.id);

    const request = await blocked.post('/friends/requests', { userId: blocker.id });
    expect(request.status).toBe(403);
    expect(request.body.error.code).toBe('BLOCKED');
    const message = await blocked.post(`/conversations/${conversation.body.id}/messages`, {
      body: 'Can you hear me?',
      clientMessageId: crypto.randomUUID(),
    });
    expect(message.status).toBe(403);
    expect((await blocked.get('/users/blk_blocker')).status).toBe(404);
    const search = await blocked.get('/users/search?q=blk_');
    expect(search.body.users.map((user: { id: string }) => user.id)).not.toContain(blocker.id);
    // The blocker still sees the profile (so they can unblock) with a restricted view.
    const view = await blocker.get('/users/blk_blocked');
    expect(view.body.relationship.state).toBe('blocked');
    expect(view.body.restricted).toBe(true);

    expect((await blocker.delete(`/blocks/${blocked.id}`)).status).toBe(204);
    expect((await blocked.post('/friends/requests', { userId: blocker.id })).status).toBe(201);
  });

  it('cancels pending requests between the users', async () => {
    const a = await registerUser(server, 'blkp_a');
    const b = await registerUser(server, 'blkp_b');
    await a.post('/friends/requests', { userId: b.id });
    await b.post('/blocks', { userId: a.id });
    expect((await b.get('/friends/requests')).body.incoming).toHaveLength(0);
    expect((await a.get('/friends/requests')).body.outgoing).toHaveLength(0);
  });
});

describe('profile privacy', () => {
  it('hides profile details from non-friends when visibility is "friends"', async () => {
    const owner = await registerUser(server, 'pv_owner');
    const friend = await registerUser(server, 'pv_friend');
    const stranger = await registerUser(server, 'pv_stranger');
    await makeFriends(owner, friend);
    await owner.patch('/profile', { bio: 'Secret bio', favoriteGames: ['Hidden Game'] });
    await owner.patch('/profile/privacy', { profileVisibility: 'friends' });

    const asStranger = await stranger.get('/users/pv_owner');
    expect(asStranger.status).toBe(200);
    expect(asStranger.body.restricted).toBe(true);
    expect(asStranger.body.bio).toBeNull();
    expect(asStranger.body.favoriteGames).toEqual([]);

    const asFriend = await friend.get('/users/pv_owner');
    expect(asFriend.body.restricted).toBe(false);
    expect(asFriend.body.bio).toBe('Secret bio');
    expect(asFriend.body.mutualFriendCount).toBe(0);
  });

  it('only lets friends message users with friends-only message privacy', async () => {
    const owner = await registerUser(server, 'mp_owner');
    const stranger = await registerUser(server, 'mp_stranger');
    const denied = await stranger.post('/conversations', { userId: owner.id });
    expect(denied.status).toBe(403);
    expect(denied.body.error.code).toBe('PRIVACY_RESTRICTED');
    await owner.patch('/profile/privacy', { messagePrivacy: 'everyone' });
    expect((await stranger.post('/conversations', { userId: owner.id })).status).toBe(200);
  });
});
