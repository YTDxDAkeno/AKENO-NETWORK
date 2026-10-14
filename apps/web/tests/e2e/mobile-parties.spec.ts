import { devices, expect, request as playwrightRequest, test, type Browser, type Page } from '@playwright/test';

/**
 * Mobile layout and party lifecycle on a phone-sized viewport, plus the Invisible status.
 * Uses its own accounts so it can run after the core acceptance scenario on the same server.
 */

const PASSWORD = 'correct-horse-battery-staple';
const ADMIN_PASSWORD = process.env.E2E_ADMIN_PASSWORD ?? 'e2e-admin-password-123';
const RUN = Date.now().toString(36).slice(-4);

async function createInvitationCodes(baseURL: string, count: number): Promise<string[]> {
  const api = await playwrightRequest.newContext({ baseURL, extraHTTPHeaders: { Origin: baseURL } });
  const login = await api.post('/api/v1/auth/login', { data: { onlineId: 'e2e_admin', password: ADMIN_PASSWORD } });
  expect(login.ok()).toBe(true);
  const { csrfToken } = (await login.json()) as { csrfToken: string };
  const codes: string[] = [];
  for (let i = 0; i < count; i += 1) {
    const created = await api.post('/api/v1/admin/invitations', {
      headers: { 'X-CSRF-Token': csrfToken },
      data: { maxUses: 1, expiresInHours: 1, note: `mobile spec ${RUN}` },
    });
    expect(created.status()).toBe(201);
    codes.push(((await created.json()) as { code: string }).code);
  }
  await api.dispose();
  return codes;
}

async function openPhone(browser: Browser): Promise<{ page: Page; errors: string[]; close: () => Promise<void> }> {
  const context = await browser.newContext({ ...devices['Pixel 7'] });
  const page = await context.newPage();
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => {
    if (message.type() === 'error' && !message.text().startsWith('Failed to load resource')) errors.push(message.text());
  });
  return { page, errors, close: () => context.close() };
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

/** Calls the API from inside the signed-in page, the same way the web client does. */
async function apiCall(page: Page, method: string, path: string, body?: unknown): Promise<{ status: number; json: any }> {
  return page.evaluate(
    async ({ method, path, body }) => {
      const session = await fetch('/api/v1/auth/session').then((response) => response.json());
      const response = await fetch(`/api/v1${path}`, {
        method,
        headers: { 'Content-Type': 'application/json', 'X-CSRF-Token': session.csrfToken },
        body: body === undefined ? undefined : JSON.stringify(body),
      });
      const text = await response.text();
      return { status: response.status, json: text ? JSON.parse(text) : null };
    },
    { method, path, body },
  );
}

async function expectNoHorizontalOverflow(page: Page): Promise<void> {
  const overflow = await page.evaluate(() => document.documentElement.scrollWidth - window.innerWidth);
  expect(overflow, `horizontal overflow on ${page.url()}`).toBeLessThanOrEqual(1);
}

test('mobile layout, party lifecycle and invisible status', async ({ browser, baseURL }) => {
  const [carolCode, daveCode] = await createInvitationCodes(baseURL!, 2);
  const carol = await openPhone(browser);
  const dave = await openPhone(browser);
  const carolId = `carol_${RUN}`;
  const daveId = `dave_${RUN}`;

  await test.step('both users register on phones and become friends', async () => {
    await register(carol.page, carolCode!, carolId, 'Carol');
    await register(dave.page, daveCode!, daveId, 'Dave');
    const daveUser = await apiCall(dave.page, 'GET', '/users/me');
    const sent = await apiCall(carol.page, 'POST', '/friends/requests', { userId: daveUser.json.id });
    expect(sent.status).toBe(201);
    expect((await apiCall(dave.page, 'POST', `/friends/requests/${sent.json.request.id}/accept`)).status).toBe(200);
  });

  await test.step('pages fit the phone width and the bottom bar and drawer navigate', async () => {
    const quickNav = carol.page.getByRole('navigation', { name: 'Quick navigation' });
    await expect(quickNav).toBeVisible();
    await expect(carol.page.getByRole('navigation', { name: 'Main' })).toBeHidden(); // desktop sidebar hidden
    for (const [label, heading] of [
      ['Friends', 'Friends'],
      ['Messages', 'Messages'],
      ['Sessions', 'Game Sessions'],
      ['Parties', 'Parties'],
      ['Home', null],
    ] as const) {
      await quickNav.getByRole('link', { name: label }).click();
      if (heading) await expect(carol.page.getByRole('heading', { name: heading, level: 1 })).toBeVisible();
      else await expect(carol.page.getByTestId('welcome-name')).toBeVisible();
      await expectNoHorizontalOverflow(carol.page);
    }
    await carol.page.getByRole('button', { name: 'Open navigation' }).click();
    const drawer = carol.page.getByRole('dialog', { name: 'Navigation' });
    await drawer.getByRole('link', { name: 'Groups' }).click();
    await expect(carol.page.getByRole('heading', { name: 'Groups', level: 1 })).toBeVisible();
    await expect(drawer).toBeHidden();
    await carol.page.goto('/settings');
    await expectNoHorizontalOverflow(carol.page);
  });

  await test.step('Carol starts a party and invites Dave', async () => {
    await carol.page.goto('/parties');
    await carol.page.getByLabel('Party name').fill('Late Night Squad');
    await carol.page.getByRole('button', { name: 'Start party' }).click();
    await expect(carol.page.getByTestId('party-name')).toHaveText('Late Night Squad');
    // Without a configured media server the party says so plainly (no fake voice controls).
    await expect(carol.page.getByTestId('party-voice')).toBeVisible();
    await expectNoHorizontalOverflow(carol.page);

    await carol.page.getByRole('button', { name: 'Invite', exact: true }).click();
    const dialog = carol.page.getByRole('dialog', { name: 'Invite friends to the party' });
    await dialog.getByRole('button', { name: 'Invite Dave' }).click();
    await expect(dialog.getByText('Invited')).toBeVisible();
    await dialog.getByRole('button', { name: 'Close dialog' }).click();
  });

  await test.step('Dave accepts from Home and both see the party', async () => {
    await dave.page.goto('/');
    const invitation = dave.page.getByTestId('invitation').filter({ hasText: 'Late Night Squad' });
    await expect(invitation).toBeVisible();
    await invitation.getByRole('button', { name: 'Accept' }).click();
    await expect(dave.page).toHaveURL(/\/parties$/);
    await expect(dave.page.getByTestId('party-members').getByRole('listitem')).toHaveCount(2);
    await expect(carol.page.getByTestId('party-members').getByRole('listitem')).toHaveCount(2); // realtime update
  });

  await test.step('party chat works on phones', async () => {
    const input = dave.page.getByTestId('message-input');
    await input.fill('On my way!');
    await dave.page.getByRole('button', { name: 'Send message' }).click();
    await expect(carol.page.getByTestId('message-log').getByText('On my way!')).toBeVisible();
  });

  await test.step('leadership passes to Dave when Carol leaves', async () => {
    await carol.page.getByRole('button', { name: 'Leave', exact: true }).click();
    await carol.page.getByRole('dialog', { name: 'Leave the party?' }).getByRole('button', { name: 'Leave party' }).click();
    await expect(carol.page.getByRole('button', { name: 'Start party' })).toBeVisible();
    const members = dave.page.getByTestId('party-members');
    await expect(members.getByRole('listitem')).toHaveCount(1);
    await expect(members.getByLabel('Party leader')).toBeVisible();
  });

  await test.step('Invisible status hides Dave from Carol until he is online again', async () => {
    await carol.page.goto('/friends?tab=all');
    const entry = carol.page.getByTestId('friend-item').filter({ hasText: 'Dave' });
    await expect(entry.getByTestId('presence-text')).toHaveText('Online');

    await dave.page.getByRole('button', { name: /Your status: Online/ }).click();
    await dave.page.getByRole('menuitem', { name: 'Invisible' }).click();
    await expect(dave.page.getByRole('button', { name: /Your status: Invisible/ })).toBeVisible();
    await expect(entry.getByTestId('presence-text')).toHaveText(/Offline|Last seen/);

    await dave.page.getByRole('button', { name: /Your status: Invisible/ }).click();
    await dave.page.getByRole('menuitem', { name: 'Do Not Disturb' }).click();
    await expect(entry.getByTestId('presence-text')).toHaveText('Do Not Disturb');
  });

  await test.step('no browser errors or CSP violations occurred', async () => {
    expect(carol.errors).toEqual([]);
    expect(dave.errors).toEqual([]);
  });

  await carol.close();
  await dave.close();
});
