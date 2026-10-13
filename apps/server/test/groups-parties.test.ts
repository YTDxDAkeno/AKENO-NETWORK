import { randomUUID } from 'node:crypto';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import {
  connectRealtime,
  createTestServer,
  listen,
  makeFriends,
  registerUser,
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

async function acceptLatestInvitation(client: TestClient): Promise<{ kind: string; targetId: string }> {
  const list = await client.get('/invitations');
  const invitation = list.body.invitations[0];
  expect(invitation).toBeDefined();
  const accepted = await client.post(`/invitations/${invitation.id}/accept`);
  expect(accepted.status).toBe(200);
  return accepted.body;
}

describe('groups', () => {
  it('creates a group, invites a user and shares a group chat', async () => {
    const owner = await registerUser(server, 'g_owner');
    const member = await registerUser(server, 'g_member');
    const outsider = await registerUser(server, 'g_outsider');

    const created = await owner.post('/groups', { name: 'Raid Night', description: 'Fridays at 8', joinPolicy: 'invite_only' });
    expect(created.status).toBe(201);
    expect(created.body).toMatchObject({ name: 'Raid Night', myRole: 'owner', memberCount: 1 });
    const groupId = created.body.id;

    // Invite-only groups are invisible to non-members.
    expect((await outsider.get(`/groups/${groupId}`)).status).toBe(404);
    expect((await outsider.post(`/groups/${groupId}/join`)).status).toBe(404);

    const invite = await owner.post(`/groups/${groupId}/invitations`, { userId: member.id });
    expect(invite.status).toBe(201);
    expect((await owner.post(`/groups/${groupId}/invitations`, { userId: member.id })).body.error.code).toBe('REQUEST_PENDING');
    expect((await owner.get(`/groups/${groupId}`)).body.pendingInvitations).toHaveLength(1);

    const preview = await member.get(`/groups/${groupId}`);
    expect(preview.status).toBe(200);
    expect(preview.body.hasPendingInvitation).toBe(true);
    expect(preview.body.conversationId).toBeNull();

    const accepted = await acceptLatestInvitation(member);
    expect(accepted).toEqual({ kind: 'group', targetId: groupId });
    const detail = await member.get(`/groups/${groupId}`);
    expect(detail.body.myRole).toBe('member');
    expect(detail.body.members.map((m: { user: { id: string } }) => m.user.id).sort()).toEqual([owner.id, member.id].sort());

    const ownerSocket = await connectRealtime(url, owner);
    const sent = await member.post(`/groups/${groupId}/messages`, { body: 'Hello group!', clientMessageId: randomUUID() });
    expect(sent.status).toBe(201);
    const event = await ownerSocket.waitFor('message.created', (e) => e.data.conversationKind === 'group');
    expect(event.data.message.body).toBe('Hello group!');
    const history = await owner.get(`/groups/${groupId}/messages`);
    expect(history.body.messages.map((m: { body: string }) => m.body)).toEqual(['Hello group!']);
    expect((await owner.get('/groups')).body.groups[0].unreadCount).toBe(1);

    // Non-members cannot read or post, even with the conversation ID.
    expect((await outsider.get(`/groups/${groupId}/messages`)).status).toBe(404);
    expect((await outsider.get(`/conversations/${detail.body.conversationId}/messages`)).status).toBe(404);
    expect(
      (await outsider.post(`/conversations/${detail.body.conversationId}/messages`, { body: 'hi', clientMessageId: randomUUID() }))
        .status,
    ).toBe(404);
    await ownerSocket.close();
  });

  it('enforces role permissions', async () => {
    const owner = await registerUser(server, 'gr_owner');
    const mod = await registerUser(server, 'gr_mod');
    const memberA = await registerUser(server, 'gr_member_a');
    const memberB = await registerUser(server, 'gr_member_b');
    const group = (await owner.post('/groups', { name: 'Permissions', joinPolicy: 'open' })).body;
    for (const client of [mod, memberA, memberB]) expect((await client.post(`/groups/${group.id}/join`)).status).toBe(200);

    expect((await memberA.patch(`/groups/${group.id}/members/${mod.id}`, { role: 'moderator' })).status).toBe(403);
    expect((await owner.patch(`/groups/${group.id}/members/${mod.id}`, { role: 'moderator' })).status).toBe(200);

    // Members cannot invite or edit; moderators can edit details but not the join policy.
    const outsider = await registerUser(server, 'gr_outsider');
    expect((await memberA.post(`/groups/${group.id}/invitations`, { userId: outsider.id })).status).toBe(403);
    expect((await memberA.patch(`/groups/${group.id}`, { name: 'Hijacked' })).status).toBe(403);
    expect((await mod.patch(`/groups/${group.id}`, { description: 'Updated by mod' })).status).toBe(200);
    expect((await mod.patch(`/groups/${group.id}`, { joinPolicy: 'invite_only' })).status).toBe(403);

    // Moderators can remove members, but not other moderators or the owner.
    expect((await mod.delete(`/groups/${group.id}/members/${memberA.id}`)).status).toBe(204);
    expect((await mod.delete(`/groups/${group.id}/members/${owner.id}`)).status).toBe(403);
    expect((await memberB.delete(`/groups/${group.id}/members/${mod.id}`)).status).toBe(403);
    const removedNotification = (await memberA.get('/notifications')).body.notifications[0];
    expect(removedNotification.data.type).toBe('group.removed');
    expect((await memberA.get(`/groups/${group.id}/messages`)).status).toBe(404);
  });

  it('requires an ownership transfer before the owner can leave', async () => {
    const owner = await registerUser(server, 'go_owner');
    const heir = await registerUser(server, 'go_heir');
    const group = (await owner.post('/groups', { name: 'Succession', joinPolicy: 'open' })).body;
    await heir.post(`/groups/${group.id}/join`);
    expect((await owner.post(`/groups/${group.id}/leave`)).status).toBe(409);
    expect((await heir.post(`/groups/${group.id}/transfer-ownership`, { userId: heir.id })).status).toBe(403);
    expect((await owner.post(`/groups/${group.id}/transfer-ownership`, { userId: heir.id })).status).toBe(200);
    const after = await heir.get(`/groups/${group.id}`);
    expect(after.body.myRole).toBe('owner');
    expect(after.body.members.find((m: { user: { id: string } }) => m.user.id === owner.id).role).toBe('moderator');
    expect((await owner.post(`/groups/${group.id}/leave`)).body).toEqual({ deleted: false });
    // The last member leaving deletes the group.
    expect((await heir.post(`/groups/${group.id}/leave`)).body).toEqual({ deleted: true });
    expect((await heir.get(`/groups/${group.id}`)).status).toBe(404);
  });

  it('lets only the owner delete a group and removes its chat', async () => {
    const owner = await registerUser(server, 'gd_owner');
    const member = await registerUser(server, 'gd_member');
    const group = (await owner.post('/groups', { name: 'Doomed', joinPolicy: 'open' })).body;
    await member.post(`/groups/${group.id}/join`);
    expect((await member.delete(`/groups/${group.id}`)).status).toBe(403);
    expect((await owner.delete(`/groups/${group.id}`)).status).toBe(204);
    expect((await member.get(`/groups/${group.id}`)).status).toBe(404);
    expect((await member.get('/groups')).body.groups).toHaveLength(0);
  });

  it('lists open groups in discovery', async () => {
    const owner = await registerUser(server, 'gdisc_owner');
    const viewer = await registerUser(server, 'gdisc_viewer');
    await owner.post('/groups', { name: 'Open Lounge', joinPolicy: 'open' });
    await owner.post('/groups', { name: 'Secret Club', joinPolicy: 'invite_only' });
    const names = (await viewer.get('/groups/discover')).body.groups.map((g: { name: string }) => g.name);
    expect(names).toContain('Open Lounge');
    expect(names).not.toContain('Secret Club');
  });
});

describe('parties', () => {
  it('creates a party, invites a friend and chats', async () => {
    const leader = await registerUser(server, 'p_leader');
    const friend = await registerUser(server, 'p_friend');
    const stranger = await registerUser(server, 'p_stranger');
    await makeFriends(leader, friend);

    const party = await leader.post('/parties', { privacy: 'invite_only', maxMembers: 4 });
    expect(party.status).toBe(201);
    expect(party.body).toMatchObject({ name: "p_leader's Party", leaderId: leader.id, memberCount: 1 });
    expect(party.body.voice.available).toBe(false);

    expect((await leader.post(`/parties/${party.body.id}/invitations`, { userId: stranger.id })).status).toBe(403);
    expect((await stranger.get(`/parties/${party.body.id}`)).status).toBe(404);
    expect((await friend.post(`/parties/${party.body.id}/join`)).status).toBe(404); // invite-only without invitation

    const leaderSocket = await connectRealtime(url, leader);
    expect((await leader.post(`/parties/${party.body.id}/invitations`, { userId: friend.id })).status).toBe(201);
    await acceptLatestInvitation(friend);
    await leaderSocket.waitFor('party.changed', (event) => event.data.partyId === party.body.id && !event.data.removed);

    const current = await friend.get('/parties/current');
    expect(current.body.party.members.map((m: { user: { id: string } }) => m.user.id)).toEqual([leader.id, friend.id]);

    await friend.post(`/parties/${party.body.id}/messages`, { body: 'GG!', clientMessageId: randomUUID() });
    const chat = await leader.get(`/parties/${party.body.id}/messages`);
    expect(chat.body.messages[0].body).toBe('GG!');
    expect((await stranger.get(`/parties/${party.body.id}/messages`)).status).toBe(404);
    await leaderSocket.close();
  });

  it('hands leadership to the next member and deletes empty parties', async () => {
    const leader = await registerUser(server, 'pl_leader');
    const second = await registerUser(server, 'pl_second');
    await makeFriends(leader, second);
    const party = (await leader.post('/parties', { privacy: 'friends' })).body;
    expect((await second.post(`/parties/${party.id}/join`)).status).toBe(200);
    expect((await leader.post(`/parties/${party.id}/leave`)).status).toBe(204);
    const after = await second.get(`/parties/${party.id}`);
    expect(after.body.leaderId).toBe(second.id);
    expect(after.body.memberCount).toBe(1);
    expect((await second.post(`/parties/${party.id}/leave`)).status).toBe(204);
    expect((await second.get(`/parties/${party.id}`)).status).toBe(404);
  });

  it('enforces capacity and keeps users in one party at a time', async () => {
    const leader = await registerUser(server, 'pc_leader');
    const one = await registerUser(server, 'pc_one');
    const two = await registerUser(server, 'pc_two');
    await makeFriends(leader, one);
    await makeFriends(leader, two);
    const party = (await leader.post('/parties', { privacy: 'friends', maxMembers: 2 })).body;
    const joins = await Promise.all([one.post(`/parties/${party.id}/join`), two.post(`/parties/${party.id}/join`)]);
    expect(joins.map((join) => join.status).sort()).toEqual([200, 409]);
    expect(joins.find((join) => join.status === 409)?.body.error.code).toBe('CAPACITY_REACHED');

    // Creating a new party leaves the previous one.
    const member = joins[0]!.status === 200 ? one : two;
    const newParty = (await member.post('/parties', {})).body;
    expect((await member.get('/parties/current')).body.party.id).toBe(newParty.id);
    expect((await leader.get(`/parties/${party.id}`)).body.memberCount).toBe(1);
  });

  it('lets only the leader kick members, transfer leadership and close the party', async () => {
    const leader = await registerUser(server, 'pk_leader');
    const member = await registerUser(server, 'pk_member');
    const other = await registerUser(server, 'pk_other');
    await makeFriends(leader, member);
    await makeFriends(leader, other);
    const party = (await leader.post('/parties', { privacy: 'friends' })).body;
    await member.post(`/parties/${party.id}/join`);
    await other.post(`/parties/${party.id}/join`);
    expect((await member.delete(`/parties/${party.id}/members/${other.id}`)).status).toBe(403);
    expect((await leader.delete(`/parties/${party.id}/members/${other.id}`)).status).toBe(204);
    expect((await other.get('/parties/current')).body.party).toBeNull();
    expect((await member.post(`/parties/${party.id}/close`)).status).toBe(403);
    expect((await leader.post(`/parties/${party.id}/leader`, { userId: member.id })).body.leaderId).toBe(member.id);
    const memberSocket = await connectRealtime(url, member);
    expect((await member.post(`/parties/${party.id}/close`)).status).toBe(204);
    await memberSocket.waitFor('party.changed', (event) => event.data.removed);
    expect((await leader.get('/parties/current')).body.party).toBeNull();
    await memberSocket.close();
  });

  it("lists friends' parties as joinable", async () => {
    const leader = await registerUser(server, 'pj_leader');
    const friend = await registerUser(server, 'pj_friend');
    const stranger = await registerUser(server, 'pj_stranger');
    await makeFriends(leader, friend);
    const party = (await leader.post('/parties', { privacy: 'friends' })).body;
    expect((await friend.get('/parties')).body.parties.map((p: { id: string }) => p.id)).toContain(party.id);
    expect((await stranger.get('/parties')).body.parties).toHaveLength(0);
    expect((await stranger.post(`/parties/${party.id}/join`)).status).toBe(404);
  });
});
