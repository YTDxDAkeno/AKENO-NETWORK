import { expect, test, type Browser, type BrowserContext, type Page } from '@playwright/test';

/**
 * AKENO NETWORK V1 core acceptance scenario.
 *
 * Runs against the real production server and web builds with a disposable PostgreSQL
 * database. Alice and Bob use two isolated browser contexts; Mallory is a third account used
 * to verify that private resources are not accessible to unauthorized users.
 */

const PASSWORD = 'correct-horse-battery-staple';
const ADMIN_PASSWORD = process.env.E2E_ADMIN_PASSWORD ?? 'e2e-admin-password-123';

interface Player {
  context: BrowserContext;
  page: Page;
  errors: string[];
}

async function openPlayer(browser: Browser): Promise<Player> {
  const context = await browser.newContext();
  const page = await context.newPage();
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => {
    // Content-Security-Policy violations and React errors show up here. Chrome also logs every
    // non-2xx fetch as "Failed to load resource", which is expected in the access-denial steps.
    if (message.type() === 'error' && !message.text().startsWith('Failed to load resource')) errors.push(message.text());
  });
  return { context, page, errors };
}

async function signIn(page: Page, onlineId: string, password: string): Promise<void> {
  await page.goto('/login');
  await page.getByLabel('Online ID').fill(onlineId);
  await page.getByLabel('Password').fill(password);
  await page.getByRole('button', { name: 'Sign in' }).click();
  // After signing in the app returns to the previous page (or Home), inside the signed-in shell.
  await expect(page).not.toHaveURL(/\/login$/);
  await expect(page.getByRole('button', { name: 'Sign out' })).toBeVisible();
}

async function signOut(page: Page): Promise<void> {
  await page.getByRole('button', { name: 'Sign out' }).click();
  await expect(page).toHaveURL(/\/login$/);
}

async function register(page: Page, code: string, onlineId: string, displayName: string): Promise<void> {
  await page.goto('/register');
  await page.getByLabel('Invitation code').fill(code);
  await page.getByLabel('Online ID').fill(onlineId);
  await page.getByLabel('Display name').fill(displayName);
  await page.getByLabel('Password', { exact: true }).fill(PASSWORD);
  await page.getByLabel('Confirm password').fill(PASSWORD);
  await page.getByRole('button', { name: 'Create account' }).click();
  await expect(page.getByTestId('welcome-name')).toHaveText(displayName);
}

async function createInvitationInAdminUi(page: Page, note: string): Promise<string> {
  await page.goto('/admin');
  await page.getByLabel('Note').fill(note);
  await page.getByRole('button', { name: 'Create', exact: true }).click();
  const code = page.getByTestId('created-invitation-code');
  await expect(code).toHaveText(/^[A-Z2-9]{4}(-[A-Z2-9]{4}){3}$/);
  const value = (await code.textContent())!.trim();
  await page.getByRole('button', { name: 'Done' }).click();
  return value;
}

async function sendChatMessage(page: Page, text: string): Promise<void> {
  const input = page.getByTestId('message-input');
  await input.fill(text);
  await input.press('Enter');
  await expect(page.getByTestId('message-log').getByText(text, { exact: true })).toBeVisible();
  // The optimistic copy must be replaced by the server-confirmed message.
  await expect(page.getByTestId('pending-message')).toHaveCount(0);
}

test('core acceptance: two users become friends, chat, group up and join a game session', async ({ browser, baseURL }) => {
  const admin = await openPlayer(browser);
  const alice = await openPlayer(browser);
  const bob = await openPlayer(browser);
  const mallory = await openPlayer(browser);

  let aliceCode = '';
  let bobCode = '';
  let malloryCode = '';
  let conversationUrl = '';
  let groupUrl = '';
  let sessionUrl = '';

  await test.step('administrator creates registration invitations', async () => {
    await signIn(admin.page, 'e2e_admin', ADMIN_PASSWORD);
    aliceCode = await createInvitationInAdminUi(admin.page, 'For Alice');
    bobCode = await createInvitationInAdminUi(admin.page, 'For Bob');
    malloryCode = await createInvitationInAdminUi(admin.page, 'For Mallory');
    await expect(admin.page.getByRole('cell', { name: 'For Bob' })).toBeVisible();
  });

  await test.step('1. Alice registers with a valid invitation', async () => {
    await register(alice.page, aliceCode, 'alice', 'Alice');
  });

  await test.step('2. Bob registers with another valid invitation', async () => {
    await register(bob.page, bobCode, 'bob', 'Bob');
  });

  await test.step('an already used invitation is rejected', async () => {
    await mallory.page.goto('/register');
    await mallory.page.getByLabel('Invitation code').fill(aliceCode);
    await mallory.page.getByLabel('Online ID').fill('mallory');
    await mallory.page.getByLabel('Password', { exact: true }).fill(PASSWORD);
    await mallory.page.getByLabel('Confirm password').fill(PASSWORD);
    await mallory.page.getByRole('button', { name: 'Create account' }).click();
    await expect(mallory.page.getByText('This invitation code is invalid, used up or expired.').first()).toBeVisible();
    await mallory.page.getByLabel('Invitation code').fill(malloryCode);
    await mallory.page.getByRole('button', { name: 'Create account' }).click();
    await expect(mallory.page.getByTestId('welcome-name')).toHaveText('mallory');
  });

  await test.step('3. both users sign out and sign in again', async () => {
    await signOut(alice.page);
    await signIn(alice.page, 'alice', PASSWORD);
    await signOut(bob.page);
    await signIn(bob.page, 'BOB', PASSWORD); // Online IDs are case-insensitive
  });

  await test.step('4. both users edit their profiles', async () => {
    await alice.page.goto('/settings');
    await alice.page.getByLabel('Status message').fill('Looking for a squad tonight');
    await alice.page.getByRole('radio', { name: 'Ember' }).click();
    await alice.page.getByLabel('Bio').fill('Co-op enthusiast.');
    await alice.page.getByLabel('Add a favorite game').fill('Starfall Arena');
    await alice.page.getByLabel('Add a favorite game').press('Enter');
    await alice.page.getByRole('button', { name: 'Save profile' }).click();
    await expect(alice.page.getByText('Profile saved.')).toBeVisible();

    await bob.page.goto('/settings');
    await bob.page.getByLabel('Display name').fill('Bob the Builder');
    await bob.page.getByLabel('Bio').fill('Racing games on weekends.');
    await bob.page.getByRole('radio', { name: 'Rocket' }).click();
    await bob.page.getByRole('button', { name: 'Save profile' }).click();
    await expect(bob.page.getByText('Profile saved.')).toBeVisible();

    await alice.page.goto('/u/alice');
    await expect(alice.page.getByTestId('profile-status-message')).toHaveText('Looking for a squad tonight');
    await expect(alice.page.getByTestId('profile-bio')).toHaveText('Co-op enthusiast.');
    await expect(alice.page.getByText('Starfall Arena')).toBeVisible();
  });

  await test.step('5-6. Alice searches for Bob and sends a friend request', async () => {
    await bob.page.goto('/friends?tab=requests');
    await expect(bob.page.getByText('No incoming requests.')).toBeVisible();

    await alice.page.goto('/friends');
    await alice.page.getByTestId('user-search').fill('bob');
    const result = alice.page.getByRole('list', { name: 'Search results' }).getByRole('listitem').filter({ hasText: 'Bob the Builder' });
    await expect(result).toBeVisible();
    await result.getByRole('button', { name: 'Add Bob the Builder as a friend' }).click();
    await expect(alice.page.getByText('Friend request sent.')).toBeVisible();
    await expect(result.getByText('Request sent')).toBeVisible();
  });

  await test.step('7. Bob receives the request in realtime and accepts it', async () => {
    const request = bob.page.getByTestId('incoming-request').filter({ hasText: 'Alice' });
    await expect(request).toBeVisible(); // no reload: delivered over the WebSocket
    await request.getByRole('button', { name: 'Accept' }).click();
    await expect(bob.page.getByText('Friend request accepted.')).toBeVisible();
  });

  await test.step('8-9. Alice and Bob appear as online friends', async () => {
    await alice.page.goto('/friends');
    const bobEntry = alice.page.getByTestId('friend-item').filter({ hasText: 'Bob the Builder' });
    await expect(bobEntry).toBeVisible();
    await expect(bobEntry.getByText('Online', { exact: true })).toBeVisible();

    await bob.page.goto('/friends');
    const aliceEntry = bob.page.getByTestId('friend-item').filter({ hasText: 'Alice' });
    await expect(aliceEntry).toBeVisible();
    await expect(aliceEntry.getByText('Online', { exact: true })).toBeVisible();
  });

  await test.step('10-11. Alice messages Bob and Bob receives it without reloading', async () => {
    await bob.page.goto('/messages');
    await alice.page.getByRole('button', { name: 'Message Bob the Builder' }).click();
    await expect(alice.page).toHaveURL(/\/messages\/[0-9a-f-]{36}$/);
    conversationUrl = new URL(alice.page.url()).pathname;
    await sendChatMessage(alice.page, 'Hey Bob! Ready for tonight?');

    const conversation = bob.page.getByRole('navigation').getByRole('link').filter({ hasText: 'Alice' });
    await expect(conversation).toBeVisible();
    await conversation.click();
    await expect(bob.page.getByTestId('message-log').getByText('Hey Bob! Ready for tonight?')).toBeVisible();
  });

  await test.step('12-13. Bob replies and Alice receives the reply in realtime', async () => {
    await sendChatMessage(bob.page, 'Absolutely. Party at 8?');
    await expect(alice.page.getByTestId('message-log').getByText('Absolutely. Party at 8?')).toBeVisible();
  });

  await test.step('14. both users still see the history after reloading', async () => {
    for (const player of [alice, bob]) {
      await player.page.goto(conversationUrl);
      const log = player.page.getByTestId('message-log');
      await expect(log.getByText('Hey Bob! Ready for tonight?')).toBeVisible();
      await expect(log.getByText('Absolutely. Party at 8?')).toBeVisible();
      await expect(log.getByTestId('message')).toHaveCount(2);
    }
  });

  await test.step('15. Alice creates a group', async () => {
    await alice.page.goto('/groups');
    await alice.page.getByRole('button', { name: 'Create group' }).first().click();
    const dialog = alice.page.getByRole('dialog', { name: 'Create a group' });
    await dialog.getByLabel('Group name').fill('Night Raiders');
    await dialog.getByLabel('Description').fill('Weekly co-op evenings.');
    await dialog.getByRole('button', { name: 'Create group' }).click();
    await expect(alice.page.getByTestId('group-name')).toHaveText('Night Raiders');
    groupUrl = new URL(alice.page.url()).pathname;
  });

  await test.step('16. Bob joins the group through an authorized invitation', async () => {
    await alice.page.getByRole('button', { name: 'Invite', exact: true }).click();
    const dialog = alice.page.getByRole('dialog', { name: 'Invite to Night Raiders' });
    await dialog.getByRole('button', { name: 'Invite Bob the Builder' }).click();
    await expect(dialog.getByText('Invited')).toBeVisible();
    await dialog.getByRole('button', { name: 'Close dialog' }).click();

    await bob.page.goto('/');
    const invitation = bob.page.getByTestId('invitation').filter({ hasText: 'Night Raiders' });
    await expect(invitation).toBeVisible();
    await invitation.getByRole('button', { name: 'Accept' }).click();
    await expect(bob.page).toHaveURL(new RegExp(`${groupUrl}$`));
    await expect(bob.page.getByTestId('group-members').getByText('Alice')).toBeVisible();
  });

  await test.step('17. group chat works in both directions', async () => {
    await alice.page.goto(groupUrl);
    await sendChatMessage(bob.page, 'Thanks for the invite!');
    await expect(alice.page.getByTestId('message-log').getByText('Thanks for the invite!')).toBeVisible();
    await sendChatMessage(alice.page, 'Welcome to Night Raiders.');
    await expect(bob.page.getByTestId('message-log').getByText('Welcome to Night Raiders.')).toBeVisible();
  });

  await test.step('18. Alice creates a game session', async () => {
    await alice.page.goto('/sessions');
    await alice.page.getByRole('button', { name: 'Host session' }).first().click();
    const dialog = alice.page.getByRole('dialog', { name: 'Host a game session' });
    await dialog.getByLabel('Game', { exact: true }).fill('Starfall Arena');
    await dialog.getByLabel('Platform (label only)').selectOption('PC');
    await dialog.getByLabel('Who can see it').selectOption('friends');
    await dialog.getByLabel('Connection details').fill('Lobby code 4242');
    await dialog.getByRole('button', { name: 'Create session' }).click();
    await expect(alice.page.getByTestId('session-title')).toHaveText('Starfall Arena');
    sessionUrl = new URL(alice.page.url()).pathname;
  });

  await test.step('19. Bob discovers the session and joins it', async () => {
    await bob.page.goto('/sessions');
    const card = bob.page.getByTestId('session-card').filter({ hasText: 'Starfall Arena' });
    await expect(card).toBeVisible();
    await card.click();
    await expect(bob.page.getByTestId('connection-info')).toHaveCount(0); // hidden until joined
    await bob.page.getByRole('button', { name: 'Join session' }).click();
    await expect(bob.page.getByText('You joined the session.')).toBeVisible();
    await expect(bob.page.getByTestId('connection-info')).toHaveText('Lobby code 4242');
  });

  await test.step('20. both users see the correct participant list', async () => {
    for (const player of [alice, bob]) {
      await player.page.goto(sessionUrl);
      const members = player.page.getByTestId('session-members');
      await expect(members.getByRole('listitem')).toHaveCount(2);
      await expect(members.getByText('Alice')).toBeVisible();
      await expect(members.getByText('Bob the Builder')).toBeVisible();
      await expect(player.page.getByRole('heading', { name: 'Players (2/4)' })).toBeVisible();
    }
  });

  await test.step('21. unauthorized users are denied access to private resources', async () => {
    await mallory.page.goto(groupUrl);
    await expect(mallory.page.getByText('Group not found')).toBeVisible();
    await mallory.page.goto(conversationUrl);
    await expect(mallory.page.getByText('Conversation not found')).toBeVisible();
    await mallory.page.goto(sessionUrl);
    await expect(mallory.page.getByText('Session not found')).toBeVisible();

    // Direct API access is refused as well, using Mallory's own valid session.
    const conversationId = conversationUrl.split('/').pop();
    const groupId = groupUrl.split('/').pop();
    const sessionId = sessionUrl.split('/').pop();
    const request = mallory.page.request;
    expect((await request.get(`/api/v1/conversations/${conversationId}/messages`)).status()).toBe(404);
    expect((await request.get(`/api/v1/groups/${groupId}/messages`)).status()).toBe(404);
    expect((await request.get(`/api/v1/game-sessions/${sessionId}`)).status()).toBe(404);
    expect((await request.get('/api/v1/admin/invitations')).status()).toBe(403);
    // State-changing requests without the CSRF token are rejected.
    const forged = await request.post(`/api/v1/conversations/${conversationId}/messages`, {
      headers: { Origin: baseURL! },
      data: { body: 'Let me in', clientMessageId: '6f9619ff-8b86-4d11-b42d-00c04fc964ff' },
    });
    expect(forged.status()).toBe(403);
  });

  await test.step('22. logout and reconnection update presence', async () => {
    await alice.page.goto('/friends');
    const bobEntry = alice.page.getByTestId('friend-item').filter({ hasText: 'Bob the Builder' });
    await expect(bobEntry.getByText('Online', { exact: true })).toBeVisible();

    await signOut(bob.page);
    await expect(alice.page.getByText('Nobody is online right now')).toBeVisible(); // pushed in realtime
    await alice.page.getByRole('tab', { name: /All friends/ }).click();
    await expect(bobEntry.getByText(/Last seen/)).toBeVisible();

    // A message sent while Bob is away is waiting for him after he signs in again.
    await alice.page.goto(conversationUrl);
    await sendChatMessage(alice.page, 'See you at 8!');

    await signIn(bob.page, 'bob', PASSWORD);
    await alice.page.goto('/friends');
    await expect(alice.page.getByTestId('friend-item').filter({ hasText: 'Bob the Builder' }).getByText('Online', { exact: true })).toBeVisible();

    await bob.page.goto(conversationUrl);
    await expect(bob.page.getByTestId('message-log').getByText('See you at 8!')).toBeVisible();

    // Reloading reconnects the WebSocket; Bob stays online for Alice the whole time.
    await bob.page.reload();
    await expect(bob.page.getByTestId('connection-status')).toHaveAttribute('data-status', 'open');
    await expect(alice.page.getByTestId('friend-item').filter({ hasText: 'Bob the Builder' }).getByText('Online', { exact: true })).toBeVisible();
  });

  await test.step('no browser errors or CSP violations occurred', async () => {
    for (const [name, player] of Object.entries({ admin, alice, bob, mallory })) {
      expect(player.errors, `${name} console errors`).toEqual([]);
    }
  });

  for (const player of [admin, alice, bob, mallory]) await player.context.close();
});
