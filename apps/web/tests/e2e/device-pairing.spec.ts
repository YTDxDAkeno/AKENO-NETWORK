import { expect, request as playwrightRequest, test } from '@playwright/test';

/**
 * Device pairing as a console client would use it: the "device" is a plain HTTP client without
 * cookies or an Origin header; the user approves its code in the real web UI, chooses
 * permissions, and later unpairs it from Settings.
 */

const PASSWORD = 'correct-horse-battery-staple';
const ADMIN_PASSWORD = process.env.E2E_ADMIN_PASSWORD ?? 'e2e-admin-password-123';
const RUN = Date.now().toString(36).slice(-4);

async function createInvitationCode(baseURL: string): Promise<string> {
  const api = await playwrightRequest.newContext({ baseURL, extraHTTPHeaders: { Origin: baseURL } });
  const login = await api.post('/api/v1/auth/login', { data: { onlineId: 'e2e_admin', password: ADMIN_PASSWORD } });
  expect(login.ok()).toBe(true);
  const { csrfToken } = (await login.json()) as { csrfToken: string };
  const created = await api.post('/api/v1/admin/invitations', {
    headers: { 'X-CSRF-Token': csrfToken },
    data: { maxUses: 1, expiresInHours: 1, note: `pairing spec ${RUN}` },
  });
  expect(created.status()).toBe(201);
  const { code } = (await created.json()) as { code: string };
  await api.dispose();
  return code;
}

test('pair a device with a code, limit its permissions and unpair it', async ({ page, baseURL }) => {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));

  const code = await createInvitationCode(baseURL!);
  const onlineId = `erin_${RUN}`;
  await page.goto('/register');
  await page.getByLabel('Invitation code').fill(code);
  await page.getByLabel('Online ID').fill(onlineId);
  await page.getByLabel('Display name').fill('Erin');
  await page.getByLabel('Password', { exact: true }).fill(PASSWORD);
  await page.getByLabel('Confirm password').fill(PASSWORD);
  await page.getByRole('button', { name: 'Create account' }).click();
  await expect(page.getByTestId('welcome-name')).toHaveText('Erin');

  // The device: no cookies, no Origin header.
  const device = await playwrightRequest.newContext({ baseURL });
  const started = await device.post('/api/v1/devices/pairings', {
    data: { deviceName: 'Test console', platform: 'PlayStation 5 (homebrew)', clientVersion: '0.1.0' },
  });
  expect(started.status()).toBe(201);
  const pairing = (await started.json()) as { deviceCode: string; userCode: string; verificationUriComplete: string };
  expect(pairing.verificationUriComplete).toBe(`${baseURL}/pair?code=${pairing.userCode}`);

  // The user follows the link (e.g. from a QR code); the code is pre-filled.
  await page.goto(pairing.verificationUriComplete);
  await expect(page.getByLabel('Pairing code')).toHaveValue(pairing.userCode);
  await page.getByRole('button', { name: 'Continue' }).click();
  await expect(page.getByRole('heading', { name: 'Test console' })).toBeVisible();
  await page.getByLabel('Send messages as you').uncheck();
  await page.getByRole('button', { name: 'Approve device' }).click();
  await expect(page.getByRole('heading', { name: 'Test console is paired' })).toBeVisible();

  const redeemed = await device.post('/api/v1/devices/pairings/token', { data: { deviceCode: pairing.deviceCode } });
  expect(redeemed.status()).toBe(200);
  const { accessToken } = (await redeemed.json()) as { accessToken: string };
  const auth = { Authorization: `Bearer ${accessToken}` };

  const self = await device.get('/api/v1/devices/current', { headers: auth });
  expect(self.status()).toBe(200);
  const selfBody = (await self.json()) as { user: { onlineId: string }; device: { scopes: string[] } };
  expect(selfBody.user.onlineId).toBe(onlineId);
  expect(selfBody.device.scopes).not.toContain('messages:write');
  expect((await device.get('/api/v1/friends', { headers: auth })).status()).toBe(200);
  const denied = await device.post('/api/v1/conversations', { headers: auth, data: { userId: '00000000-0000-4000-8000-000000000000' } });
  expect(denied.status()).toBe(403);
  expect(((await denied.json()) as { error: { code: string } }).error.code).toBe('DEVICE_SCOPE_DENIED');

  // Account Bridge: the console links its local user; the website labels it local and unverified.
  const linked = await device.put('/api/v1/devices/current/console-user', {
    headers: auth,
    data: { localUserId: 268435457, userName: 'Local Player', accountState: 'no_account_id' },
  });
  expect(linked.status()).toBe(200);

  // Unpair from Settings → Account & security.
  await page.goto('/settings?tab=security');
  const card = page.locator('section', { has: page.getByRole('heading', { name: 'Paired devices' }) });
  await expect(card.getByText('Test console')).toBeVisible();
  const consoleUser = card.getByTestId('console-user');
  await expect(consoleUser).toContainText('Local Player');
  await expect(consoleUser).toContainText('not verified by Sony');
  await expect(consoleUser).toContainText('offline user, no account ID');
  await consoleUser.getByRole('button', { name: 'Unlink' }).click();
  await expect(card.getByTestId('console-user')).toHaveCount(0);
  expect(((await (await device.get('/api/v1/devices/current', { headers: auth })).json()) as { device: { consoleUser: unknown } }).device.consoleUser).toBeNull();
  await card.getByRole('button', { name: 'Unpair' }).click();
  await page.getByRole('dialog').getByRole('button', { name: 'Unpair' }).click();
  await expect(card.getByText('No devices are paired with your account.')).toBeVisible();
  expect((await device.get('/api/v1/devices/current', { headers: auth })).status()).toBe(401);

  await device.dispose();
  expect(errors).toEqual([]);
});
