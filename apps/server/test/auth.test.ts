import { eq, sql } from 'drizzle-orm';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import type { AuthSessionResponse } from '@akeno/shared';
import { authSessions, registrationInvitations, users } from '../src/database/schema/index.js';
import {
  DEFAULT_PASSWORD,
  TestClient,
  createInvitationCode,
  createTestServer,
  login,
  makeFriends,
  registerUser,
  type TestServer,
} from './helpers.js';

let server: TestServer;

beforeAll(async () => {
  server = await createTestServer();
});

afterAll(async () => {
  await server.close();
});

describe('registration', () => {
  it('registers a user with a valid invitation and starts a session', async () => {
    const client = await registerUser(server, 'Alice', { displayName: 'Alice Akeno' });
    expect(client.user?.onlineId).toBe('Alice');
    expect(client.user?.profile.displayName).toBe('Alice Akeno');
    expect(client.cookie).toMatch(/^akeno_session=/);
    const session = await client.get<AuthSessionResponse>('/auth/session');
    expect(session.status).toBe(200);
    expect(session.body.user.id).toBe(client.id);
    expect(session.body.csrfToken).toBe(client.csrf);
  });

  it('stores only an Argon2id hash, never the plaintext password', async () => {
    const client = await registerUser(server, 'hashcheck');
    const [row] = await server.ctx.db.select().from(users).where(eq(users.id, client.id));
    expect(row?.passwordHash).toMatch(/^\$argon2id\$/);
    expect(row?.passwordHash).not.toContain(DEFAULT_PASSWORD);
  });

  it('requires an invitation code in invite mode', async () => {
    const client = new TestClient(server.app);
    const response = await client.post('/auth/register', { onlineId: 'nocode', password: DEFAULT_PASSWORD });
    expect(response.status).toBe(400);
    expect(response.body.error.code).toBe('INVITATION_REQUIRED');
  });

  it('rejects unknown, exhausted, expired and revoked invitations', async () => {
    const client = new TestClient(server.app);
    const unknown = await client.post('/auth/register', {
      onlineId: 'badcode',
      password: DEFAULT_PASSWORD,
      invitationCode: 'AAAA-BBBB-CCCC-DDDD',
    });
    expect(unknown.body.error.code).toBe('INVITATION_INVALID');

    const code = await createInvitationCode(server.ctx, 1);
    const first = await new TestClient(server.app).post('/auth/register', {
      onlineId: 'usesone',
      password: DEFAULT_PASSWORD,
      invitationCode: code,
    });
    expect(first.status).toBe(201);
    const second = await new TestClient(server.app).post('/auth/register', {
      onlineId: 'usestwo',
      password: DEFAULT_PASSWORD,
      invitationCode: code.toLowerCase(), // codes are case-insensitive
    });
    expect(second.body.error.code).toBe('INVITATION_INVALID');

    const expired = await server.ctx.admin.createInvitation(null, { maxUses: 1, expiresInHours: 1, note: '' }, null);
    await server.ctx.db
      .update(registrationInvitations)
      .set({ expiresAt: new Date(Date.now() - 1000) })
      .where(eq(registrationInvitations.id, expired.invitation.id));
    const expiredAttempt = await new TestClient(server.app).post('/auth/register', {
      onlineId: 'expiredinv',
      password: DEFAULT_PASSWORD,
      invitationCode: expired.code,
    });
    expect(expiredAttempt.body.error.code).toBe('INVITATION_INVALID');

    const revoked = await server.ctx.admin.createInvitation(null, { maxUses: 1, expiresInHours: 1, note: '' }, null);
    await server.ctx.db
      .update(registrationInvitations)
      .set({ revokedAt: new Date() })
      .where(eq(registrationInvitations.id, revoked.invitation.id));
    const revokedAttempt = await new TestClient(server.app).post('/auth/register', {
      onlineId: 'revokedinv',
      password: DEFAULT_PASSWORD,
      invitationCode: revoked.code,
    });
    expect(revokedAttempt.body.error.code).toBe('INVITATION_INVALID');
  });

  it('enforces case-insensitive unique Online IDs without consuming the invitation', async () => {
    await registerUser(server, 'DupUser');
    const code = await createInvitationCode(server.ctx, 1);
    const duplicate = await new TestClient(server.app).post('/auth/register', {
      onlineId: 'dupuser',
      password: DEFAULT_PASSWORD,
      invitationCode: code,
    });
    expect(duplicate.status).toBe(409);
    expect(duplicate.body.error.code).toBe('ONLINE_ID_TAKEN');
    // The failed attempt rolled back, so the code still works.
    const retry = await new TestClient(server.app).post('/auth/register', {
      onlineId: 'dupuser2',
      password: DEFAULT_PASSWORD,
      invitationCode: code,
    });
    expect(retry.status).toBe(201);
  });

  it('only redeems a single-use invitation once under concurrent registrations', async () => {
    const code = await createInvitationCode(server.ctx, 1);
    const attempts = await Promise.all(
      Array.from({ length: 5 }, (_, index) =>
        new TestClient(server.app).post('/auth/register', {
          onlineId: `racer${index}`,
          password: DEFAULT_PASSWORD,
          invitationCode: code,
        }),
      ),
    );
    expect(attempts.filter((attempt) => attempt.status === 201)).toHaveLength(1);
    expect(attempts.filter((attempt) => attempt.body?.error?.code === 'INVITATION_INVALID')).toHaveLength(4);
  });

  it('validates Online ID format, reserved names and password quality', async () => {
    const code = await createInvitationCode(server.ctx, 5);
    const client = new TestClient(server.app);
    const badFormat = await client.post('/auth/register', { onlineId: '1abc', password: DEFAULT_PASSWORD, invitationCode: code });
    expect(badFormat.body.error.code).toBe('VALIDATION_FAILED');
    expect(badFormat.body.error.fields.onlineId).toBeDefined();
    const reserved = await client.post('/auth/register', { onlineId: 'Admin', password: DEFAULT_PASSWORD, invitationCode: code });
    expect(reserved.body.error.code).toBe('ONLINE_ID_TAKEN');
    const short = await client.post('/auth/register', { onlineId: 'shortpw', password: 'short', invitationCode: code });
    expect(short.body.error.fields.password).toMatch(/at least 12/);
    const common = await client.post('/auth/register', { onlineId: 'commonpw', password: 'password1234', invitationCode: code });
    expect(common.body.error.code).toBe('VALIDATION_FAILED');
    const containsId = await client.post('/auth/register', {
      onlineId: 'gamerbob',
      password: 'my-gamerbob-password',
      invitationCode: code,
    });
    expect(containsId.body.error.message).toMatch(/Online ID/);
  });

  it('allows registration without a code only when open mode is configured', async () => {
    const open = await createTestServer({ REGISTRATION_MODE: 'open' });
    try {
      const response = await new TestClient(open.app).post('/auth/register', {
        onlineId: 'openuser',
        password: DEFAULT_PASSWORD,
      });
      expect(response.status).toBe(201);
      const config = await new TestClient(open.app).get('/auth/config');
      expect(config.body.registrationMode).toBe('open');
    } finally {
      await open.close();
    }
  });
});

describe('login and sessions', () => {
  it('logs in with the same credentials from multiple devices', async () => {
    await registerUser(server, 'multidev');
    const deviceA = await login(server, 'MULTIDEV'); // case-insensitive Online ID
    const deviceB = await login(server, 'multidev');
    expect(deviceA.cookie).not.toBe(deviceB.cookie);
    expect((await deviceA.get('/users/me')).status).toBe(200);
    expect((await deviceB.get('/users/me')).status).toBe(200);
    const sessions = await deviceA.get('/auth/sessions');
    expect(sessions.body.sessions.length).toBe(3); // registration + two logins
    expect(sessions.body.sessions.filter((entry: { current: boolean }) => entry.current)).toHaveLength(1);
  });

  it('returns the same error for wrong passwords and unknown accounts', async () => {
    await registerUser(server, 'wrongpw');
    const client = new TestClient(server.app);
    const wrong = await client.post('/auth/login', { onlineId: 'wrongpw', password: 'not-the-password!' });
    const unknown = await client.post('/auth/login', { onlineId: 'nobody-here', password: 'not-the-password!' });
    expect(wrong.status).toBe(401);
    expect(unknown.status).toBe(401);
    expect(wrong.body.error.message).toBe(unknown.body.error.message);
    expect(wrong.body.error.code).toBe('INVALID_CREDENTIALS');
  });

  it('locks an account temporarily after repeated failed logins', async () => {
    await registerUser(server, 'lockme');
    const client = new TestClient(server.app);
    for (let i = 0; i < 6; i += 1) {
      await client.post('/auth/login', { onlineId: 'lockme', password: 'wrong-password-123' });
    }
    const locked = await client.post('/auth/login', { onlineId: 'lockme', password: DEFAULT_PASSWORD });
    expect(locked.status).toBe(429);
    expect(locked.body.error.code).toBe('RATE_LIMITED');
  });

  it('logout revokes the session server-side', async () => {
    const client = await registerUser(server, 'logoutuser');
    const stolenCookie = client.cookie;
    const logout = await client.post('/auth/logout');
    expect(logout.status).toBe(204);
    expect(client.cookie).toBeNull();
    const replay = new TestClient(server.app);
    replay.cookie = stolenCookie;
    expect((await replay.get('/users/me')).status).toBe(401);
  });

  it('rejects expired and idle sessions', async () => {
    const expired = await registerUser(server, 'expireduser');
    await server.ctx.db
      .update(authSessions)
      .set({ expiresAt: new Date(Date.now() - 1000) })
      .where(eq(authSessions.userId, expired.id));
    expect((await expired.get('/users/me')).status).toBe(401);

    const idle = await registerUser(server, 'idleuser');
    await server.ctx.db
      .update(authSessions)
      .set({ lastUsedAt: new Date(Date.now() - 8 * 24 * 60 * 60 * 1000) })
      .where(eq(authSessions.userId, idle.id));
    expect((await idle.get('/users/me')).status).toBe(401);
    const remaining = await server.ctx.db.select().from(authSessions).where(eq(authSessions.userId, idle.id));
    expect(remaining).toHaveLength(0);
  });

  it('stores only hashed session tokens', async () => {
    const client = await registerUser(server, 'tokenhash');
    const token = client.cookie!.split('=')[1]!;
    const rows = await server.ctx.db.select().from(authSessions).where(eq(authSessions.userId, client.id));
    expect(rows[0]?.tokenHash).toMatch(/^[0-9a-f]{64}$/);
    expect(rows[0]?.tokenHash).not.toBe(token);
  });

  it('changing the password signs out other sessions', async () => {
    const primary = await registerUser(server, 'changepw');
    const other = await login(server, 'changepw');
    const wrongCurrent = await primary.post('/auth/password', {
      currentPassword: 'not-my-password',
      newPassword: 'a-brand-new-password',
    });
    expect(wrongCurrent.status).toBe(400);
    const changed = await primary.post('/auth/password', {
      currentPassword: DEFAULT_PASSWORD,
      newPassword: 'a-brand-new-password',
    });
    expect(changed.status).toBe(204);
    expect((await primary.get('/users/me')).status).toBe(200);
    expect((await other.get('/users/me')).status).toBe(401);
    expect((await new TestClient(server.app).post('/auth/login', { onlineId: 'changepw', password: DEFAULT_PASSWORD })).status).toBe(401);
    await login(server, 'changepw', 'a-brand-new-password');
  });

  it('revokes individual sessions only for their owner', async () => {
    const owner = await registerUser(server, 'revoker');
    const second = await login(server, 'revoker');
    const intruder = await registerUser(server, 'intruder');
    const sessions = await owner.get('/auth/sessions');
    const target = sessions.body.sessions.find((entry: { current: boolean }) => !entry.current);
    expect((await intruder.delete(`/auth/sessions/${target.id}`)).status).toBe(404);
    expect((await owner.delete(`/auth/sessions/${target.id}`)).status).toBe(204);
    expect((await second.get('/users/me')).status).toBe(401);
  });

  it('suspended users cannot sign in and lose their sessions', async () => {
    const admin = await registerUser(server, 'suspadmin');
    await server.ctx.db.update(users).set({ role: 'admin' }).where(eq(users.id, admin.id));
    const victim = await registerUser(server, 'suspended1');
    const suspend = await admin.patch(`/admin/users/${victim.id}`, { status: 'suspended' });
    expect(suspend.status).toBe(200);
    expect((await victim.get('/users/me')).status).toBe(401);
    const attempt = await new TestClient(server.app).post('/auth/login', { onlineId: 'suspended1', password: DEFAULT_PASSWORD });
    expect(attempt.status).toBe(403);
    expect(attempt.body.error.code).toBe('ACCOUNT_SUSPENDED');
  });
});

describe('request protections', () => {
  it('requires authentication for private endpoints', async () => {
    const anonymous = new TestClient(server.app);
    for (const path of ['/users/me', '/friends', '/conversations', '/groups', '/parties/current', '/game-sessions']) {
      const response = await anonymous.get(path);
      expect(response.status, path).toBe(401);
      expect(response.body.error.code).toBe('UNAUTHENTICATED');
    }
  });

  it('requires a CSRF token for state-changing requests', async () => {
    const client = await registerUser(server, 'csrfuser');
    const withoutToken = await client.request('PATCH', '/profile', { bio: 'x' }, { csrf: false });
    expect(withoutToken.status).toBe(403);
    expect(withoutToken.body.error.code).toBe('CSRF_FAILED');
    const wrongToken = await client.request('PATCH', '/profile', { bio: 'x' }, { csrf: false, headers: { 'x-csrf-token': 'forged' } });
    expect(wrongToken.body.error.code).toBe('CSRF_FAILED');
  });

  it('rejects state-changing requests from foreign origins', async () => {
    const client = await registerUser(server, 'originuser');
    client.origin = 'https://evil.example';
    const response = await client.patch('/profile', { bio: 'x' });
    expect(response.status).toBe(403);
    expect(response.body.error.code).toBe('ORIGIN_REJECTED');
    client.origin = null;
    const noOrigin = await client.patch('/profile', { bio: 'x' });
    expect(noOrigin.body.error.code).toBe('ORIGIN_REJECTED');
  });

  it('protects administrator routes', async () => {
    const regular = await registerUser(server, 'notadmin');
    expect((await new TestClient(server.app).get('/admin/invitations')).status).toBe(401);
    const forbidden = await regular.get('/admin/invitations');
    expect(forbidden.status).toBe(403);
    expect((await regular.post('/admin/invitations', { maxUses: 1 })).status).toBe(403);
  });

  it('lets administrators manage invitations without storing the code', async () => {
    const admin = await registerUser(server, 'realadmin');
    await server.ctx.db.update(users).set({ role: 'admin' }).where(eq(users.id, admin.id));
    const created = await admin.post('/admin/invitations', { maxUses: 2, expiresInHours: 1, note: 'Friends' });
    expect(created.status).toBe(201);
    expect(created.body.code).toMatch(/^[A-Z2-9]{4}-[A-Z2-9]{4}-[A-Z2-9]{4}-[A-Z2-9]{4}$/);
    const stored = await server.ctx.db.execute<{ code_hash: string }>(
      sql`SELECT code_hash FROM registration_invitations WHERE id = ${created.body.invitation.id}`,
    );
    expect(stored.rows[0]?.code_hash).not.toContain(created.body.code.replace(/-/g, ''));
    const list = await admin.get('/admin/invitations');
    expect(list.body.invitations.some((inv: { id: string }) => inv.id === created.body.invitation.id)).toBe(true);
    expect(JSON.stringify(list.body)).not.toContain(created.body.code);
    const revoked = await admin.delete(`/admin/invitations/${created.body.invitation.id}`);
    expect(revoked.body.status).toBe('revoked');
    const audit = await admin.get('/admin/audit-events');
    expect(audit.body.events.map((event: { action: string }) => event.action)).toContain('admin.invitation_created');
  });
});

describe('profiles and account deletion', () => {
  it('updates profiles with validation', async () => {
    const client = await registerUser(server, 'profileuser');
    const updated = await client.patch('/profile', {
      displayName: '  Pro   Gamer ',
      avatar: 'ember',
      bio: 'Line one\nLine two',
      statusMessage: 'Ready',
      favoriteGames: ['Racer', 'racer', 'Puzzle Quest'],
      activity: 'Playing a racing game',
    });
    expect(updated.status).toBe(200);
    expect(updated.body.profile).toMatchObject({
      displayName: 'Pro Gamer',
      avatar: 'ember',
      bio: 'Line one\nLine two',
      favoriteGames: ['Racer', 'Puzzle Quest'],
      activity: 'Playing a racing game',
    });
    const invalidAvatar = await client.patch('/profile', { avatar: 'not-real' });
    expect(invalidAvatar.status).toBe(400);
    const tooLong = await client.patch('/profile', { bio: 'x'.repeat(501) });
    expect(tooLong.body.error.fields.bio).toBeDefined();
  });

  it('deletes an account safely and releases the Online ID', async () => {
    const leaving = await registerUser(server, 'leaver');
    const friend = await registerUser(server, 'stayer');
    await makeFriends(leaving, friend);
    const conversation = await leaving.post('/conversations', { userId: friend.id });
    await leaving.post(`/conversations/${conversation.body.id}/messages`, {
      body: 'Goodbye!',
      clientMessageId: crypto.randomUUID(),
    });
    const group = await leaving.post('/groups', { name: 'Leaver Group' });
    await leaving.post(`/groups/${group.body.id}/invitations`, { userId: friend.id });
    const invitation = (await friend.get('/invitations')).body.invitations[0];
    await friend.post(`/invitations/${invitation.id}/accept`);

    const wrong = await leaving.delete('/users/me', { password: 'wrong-password-x', confirmation: 'DELETE' });
    expect(wrong.status).toBe(400);
    const deleted = await leaving.delete('/users/me', { password: DEFAULT_PASSWORD, confirmation: 'DELETE' });
    expect(deleted.status).toBe(204);

    expect((await friend.get('/friends')).body.friends).toHaveLength(0);
    const history = await friend.get(`/conversations/${conversation.body.id}/messages`);
    expect(history.body.messages[0].sender).toMatchObject({ deleted: true, displayName: 'Deleted user' });
    const transferred = await friend.get(`/groups/${group.body.id}`);
    expect(transferred.body.myRole).toBe('owner');
    expect((await new TestClient(server.app).post('/auth/login', { onlineId: 'leaver', password: DEFAULT_PASSWORD })).status).toBe(401);
    // The Online ID can be registered again by someone new.
    const reuse = await registerUser(server, 'leaver');
    expect(reuse.user?.onlineId).toBe('leaver');
  });
});
