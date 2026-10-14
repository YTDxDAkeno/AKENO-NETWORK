import { devices, expect, request as playwrightRequest, test, type APIRequestContext, type Browser, type Page } from '@playwright/test';

/**
 * Automatic game activity as AKENO for PS5 reports it: a paired "console" (a plain HTTP client
 * with a device token) sends heartbeats for the game it detected; a friend sees the game in the
 * friend list and on the profile in realtime; privacy settings hide it; history can be cleared.
 */

const PASSWORD = 'correct-horse-battery-staple';
const ADMIN_PASSWORD = process.env.E2E_ADMIN_PASSWORD ?? 'e2e-admin-password-123';
const RUN = Date.now().toString(36).slice(-4);
const GAME = { titleId: 'PPSA01234', titleName: 'Astro Test Drive', platform: 'PS5' };

async function createInvitationCodes(baseURL: string, count: number): Promise<string[]> {
  const api = await playwrightRequest.newContext({ baseURL, extraHTTPHeaders: { Origin: baseURL } });
  const login = await api.post('/api/v1/auth/login', { data: { onlineId: 'e2e_admin', password: ADMIN_PASSWORD } });
  expect(login.ok()).toBe(true);
  const { csrfToken } = (await login.json()) as { csrfToken: string };
  const codes: string[] = [];
  for (let i = 0; i < count; i += 1) {
    const created = await api.post('/api/v1/admin/invitations', {
      headers: { 'X-CSRF-Token': csrfToken },
      data: { maxUses: 1, expiresInHours: 1, note: `game activity spec ${RUN}` },
    });
    expect(created.status()).toBe(201);
    codes.push(((await created.json()) as { code: string }).code);
  }
  await api.dispose();
  return codes;
}

async function openUser(browser: Browser): Promise<{ page: Page; errors: string[]; close: () => Promise<void> }> {
  const context = await browser.newContext({ ...devices['Desktop Chrome'], viewport: { width: 1366, height: 860 } });
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

test('a paired console shares the game being played; friends see it live and privacy hides it', async ({ browser, baseURL }) => {
  const [frankCode, graceCode] = await createInvitationCodes(baseURL!, 2);
  const frank = await openUser(browser);
  const grace = await openUser(browser);
  const frankId = `frank_${RUN}`;
  let console_: APIRequestContext;
  let auth: Record<string, string> = {};
  const heartbeat = () => console_.post('/api/v1/activity/heartbeat', { headers: auth, data: GAME });

  await test.step('Frank and Grace register and become friends', async () => {
    await register(frank.page, frankCode!, frankId, 'Frank');
    await register(grace.page, graceCode!, `grace_${RUN}`, 'Grace');
    const graceUser = await apiCall(grace.page, 'GET', '/users/me');
    const sent = await apiCall(frank.page, 'POST', '/friends/requests', { userId: graceUser.json.id });
    expect(sent.status).toBe(201);
    expect((await apiCall(grace.page, 'POST', `/friends/requests/${sent.json.request.id}/accept`)).status).toBe(200);
  });

  await test.step('Frank pairs his console and grants the game activity permission', async () => {
    console_ = await playwrightRequest.newContext({ baseURL });
    const started = await console_.post('/api/v1/devices/pairings', {
      data: { deviceName: 'Living room PS5', platform: 'PlayStation 5 (homebrew)', clientVersion: '0.2.0' },
    });
    expect(started.status()).toBe(201);
    const pairing = (await started.json()) as { deviceCode: string; verificationUriComplete: string };
    await frank.page.goto(pairing.verificationUriComplete);
    await frank.page.getByRole('button', { name: 'Continue' }).click();
    await expect(frank.page.getByLabel('Share the game you are playing, detected by this device')).toBeChecked();
    await frank.page.getByRole('button', { name: 'Approve device' }).click();
    await expect(frank.page.getByRole('heading', { name: 'Living room PS5 is paired' })).toBeVisible();
    const redeemed = await console_.post('/api/v1/devices/pairings/token', { data: { deviceCode: pairing.deviceCode } });
    expect(redeemed.status()).toBe(200);
    auth = { Authorization: `Bearer ${((await redeemed.json()) as { accessToken: string }).accessToken}` };
  });

  await test.step('Grace sees the detected game appear in her friend list without reloading', async () => {
    await grace.page.goto('/friends?tab=all');
    const entry = grace.page.getByTestId('friend-item').filter({ hasText: 'Frank' });
    await expect(entry.getByTestId('presence-text')).toHaveText('Online');
    await expect(entry.getByTestId('game-activity-line')).toHaveCount(0);

    const first = await heartbeat();
    expect(first.status()).toBe(200);
    expect(((await first.json()) as { session: { created: boolean } }).session.created).toBe(true);
    await expect(entry.getByTestId('game-activity-line')).toContainText('Playing Astro Test Drive');
  });

  await test.step('the profile shows a game card and tracked history', async () => {
    await grace.page.goto(`/u/${frankId}`);
    const card = grace.page.getByTestId('game-activity-card');
    await expect(card).toContainText('Playing now');
    await expect(card).toContainText('Astro Test Drive');
    await expect(card).toContainText('PlayStation 5');
    const history = grace.page.getByTestId('game-history');
    await expect(history).toContainText('Astro Test Drive');
    await expect(history).toContainText('1 session');
    await expect(grace.page.getByText(/Earlier playtime is not known/)).toBeVisible();

    // The console reports the game closed: the card disappears live, the history stays.
    const stopped = await console_.post('/api/v1/activity/stop', { headers: auth, data: { reason: 'closed' } });
    expect(((await stopped.json()) as { ended: number }).ended).toBe(1);
    await expect(card).toHaveCount(0);
    await expect(history).toContainText('Last played');
  });

  await test.step('privacy: Frank hides what he is playing from everyone', async () => {
    await frank.page.goto('/settings?tab=privacy');
    await frank.page.getByLabel('Who can see what you are playing').selectOption('nobody');
    await frank.page.getByRole('button', { name: 'Save privacy settings' }).click();
    await expect(frank.page.getByText('Privacy settings saved.')).toBeVisible();

    expect((await heartbeat()).status()).toBe(200);
    await grace.page.goto('/friends?tab=all');
    const entry = grace.page.getByTestId('friend-item').filter({ hasText: 'Frank' });
    await expect(entry.getByTestId('presence-text')).toHaveText('Online');
    await expect(entry.getByTestId('game-activity-line')).toHaveCount(0);
    const asGrace = await apiCall(grace.page, 'GET', `/users/${frankId}/activity`);
    expect(asGrace.json.current).toBeNull();

    // Frank still sees his own activity.
    await frank.page.goto(`/u/${frankId}`);
    await expect(frank.page.getByTestId('game-activity-card')).toContainText('Astro Test Drive');
  });

  await test.step('turning tracking off ends the session and rejects new heartbeats', async () => {
    await frank.page.goto('/settings?tab=privacy');
    await frank.page.getByRole('switch', { name: 'Share the game I am playing' }).click();
    await frank.page.getByRole('button', { name: 'Save privacy settings' }).click();
    await expect(frank.page.getByText('Privacy settings saved.')).toBeVisible();
    const rejected = await heartbeat();
    expect(rejected.status()).toBe(409);
    expect(((await rejected.json()) as { error: { code: string } }).error.code).toBe('ACTIVITY_TRACKING_DISABLED');

    await frank.page.getByRole('button', { name: 'Clear play history' }).click();
    await frank.page.getByRole('dialog').getByRole('button', { name: 'Clear history' }).click();
    await expect(frank.page.getByText('Play history cleared.')).toBeVisible();
    await frank.page.goto(`/u/${frankId}`);
    await expect(frank.page.getByText(/No games tracked yet/)).toBeVisible();
  });

  await test.step('no browser errors or CSP violations occurred', async () => {
    expect(frank.errors).toEqual([]);
    expect(grace.errors).toEqual([]);
  });

  await console_!.dispose();
  await frank.close();
  await grace.close();
});
