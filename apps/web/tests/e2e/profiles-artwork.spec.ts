import { mkdirSync } from 'node:fs';
import { devices, expect, request as playwrightRequest, test, type Browser, type Page } from '@playwright/test';
import { testPng } from './png';

/**
 * Alpha 0.3: custom profile pictures and banners uploaded through the real crop dialog, profile
 * themes, administrator-granted badges, and game artwork that appears automatically for a title
 * a (simulated) console reports. Everything runs against the production builds; nothing is mocked.
 */

const PASSWORD = 'correct-horse-battery-staple';
const ADMIN_PASSWORD = process.env.E2E_ADMIN_PASSWORD ?? 'e2e-admin-password-123';
const RUN = Date.now().toString(36).slice(-4);
const GAME = { titleId: 'PPSA07777', name: `Aurora Drift ${RUN}` };
const SHOTS = 'test-results/screenshots';

test.describe.configure({ mode: 'serial' });

interface UserPage {
  page: Page;
  errors: string[];
  close: () => Promise<void>;
}

async function openUser(browser: Browser, mobile = false): Promise<UserPage> {
  const context = await browser.newContext(mobile ? { ...devices['Pixel 7'] } : { ...devices['Desktop Chrome'], viewport: { width: 1366, height: 900 } });
  const page = await context.newPage();
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => {
    if (message.type() === 'error' && !message.text().startsWith('Failed to load resource')) errors.push(message.text());
  });
  return { page, errors, close: () => context.close() };
}

async function signIn(page: Page, onlineId: string, password = PASSWORD): Promise<boolean> {
  await page.goto('/login');
  await page.getByLabel('Online ID').fill(onlineId);
  await page.getByLabel('Password').fill(password);
  await page.getByRole('button', { name: 'Sign in' }).click();
  // The sign-out button lives in the drawer on phones, so wait for leaving /login instead.
  return Promise.race([
    page.waitForURL((url) => !url.pathname.startsWith('/login')).then(() => true),
    page.getByRole('alert').waitFor().then(() => false),
  ]);
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

/**
 * Alice and Bob are created by the core acceptance spec (registration is rate-limited per IP for
 * the whole suite); when this spec runs on its own it registers them.
 */
async function ensureUser(page: Page, admin: Page, onlineId: string, displayName: string): Promise<void> {
  if (await signIn(page, onlineId)) return;
  const created = await apiCall(admin, 'POST', '/admin/invitations', { maxUses: 1, expiresInHours: 1, note: 'profiles spec' });
  await page.goto('/register');
  await page.getByLabel('Invitation code').fill(created.json.code);
  await page.getByLabel('Online ID').fill(onlineId);
  await page.getByLabel('Display name').fill(displayName);
  await page.getByLabel('Password', { exact: true }).fill(PASSWORD);
  await page.getByLabel('Confirm password').fill(PASSWORD);
  await page.getByRole('button', { name: 'Create account' }).click();
  await expect(page.getByTestId('welcome-name')).toBeVisible();
}

async function noHorizontalOverflow(page: Page): Promise<void> {
  const overflow = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
  expect(overflow).toBeLessThanOrEqual(1);
}

test('custom pictures, banners, themes, badges and game artwork', async ({ browser, baseURL }) => {
  mkdirSync(SHOTS, { recursive: true });
  const admin = await openUser(browser);
  const alice = await openUser(browser);
  const bob = await openUser(browser);

  await test.step('everyone signs in; Alice and Bob are friends', async () => {
    expect(await signIn(admin.page, 'e2e_admin', ADMIN_PASSWORD)).toBe(true);
    await ensureUser(alice.page, admin.page, 'alice', 'Alice');
    await ensureUser(bob.page, admin.page, 'bob', 'Bob');
    const bobUser = await apiCall(bob.page, 'GET', '/users/me');
    const friends = await apiCall(alice.page, 'GET', '/friends');
    if (!friends.json.friends.some((friend: { id: string }) => friend.id === bobUser.json.id)) {
      const sent = await apiCall(alice.page, 'POST', '/friends/requests', { userId: bobUser.json.id });
      if (sent.status === 201 && sent.json.request) await apiCall(bob.page, 'POST', `/friends/requests/${sent.json.request.id}/accept`);
    }
  });

  await test.step('a file that is not an image is refused in the crop dialog', async () => {
    await alice.page.goto('/settings');
    await alice.page.getByRole('button', { name: /^(Upload|Replace)$/ }).first().click();
    await alice.page.getByTestId('avatar-file-input').setInputFiles({ name: 'photo.png', mimeType: 'image/png', buffer: Buffer.from('<svg onload=alert(1)>') });
    await expect(alice.page.getByRole('dialog')).toContainText('could not be opened as an image');
    await alice.page.getByRole('dialog').getByRole('button', { name: 'Cancel' }).click();
  });

  await test.step('Alice uploads a profile picture: crop, zoom, save', async () => {
    await alice.page.getByRole('button', { name: /^(Upload|Replace)$/ }).first().click();
    const dialog = alice.page.getByRole('dialog');
    await dialog.getByTestId('avatar-file-input').setInputFiles({ name: 'me.png', mimeType: 'image/png', buffer: testPng(900, 700, [233, 120, 60], [40, 70, 160]) });
    const frame = dialog.getByTestId('crop-frame');
    await expect(frame).toBeVisible();
    await dialog.getByRole('button', { name: 'Zoom in' }).click();
    // Reposition with the keyboard (works with a controller-driven browser too).
    await frame.focus();
    await alice.page.keyboard.press('ArrowLeft');
    await alice.page.screenshot({ path: `${SHOTS}/crop-dialog.png` });
    await dialog.getByRole('button', { name: 'Save picture' }).click();
    await expect(alice.page.getByText('Profile picture updated.')).toBeVisible();
    await expect(dialog).toHaveCount(0);
    const me = await apiCall(alice.page, 'GET', '/users/me');
    expect(me.json.profile.avatarImage).toMatch(/^[0-9a-f]{32}$/);
    // The header avatar now shows the uploaded image (loaded from /media with WebP).
    const headerImage = alice.page.locator('header img[src^="/media/"]').first();
    await expect(headerImage).toBeVisible();
    expect(await headerImage.evaluate((img: HTMLImageElement) => img.complete && img.naturalWidth > 0)).toBe(true);
  });

  await test.step('Alice uploads a banner and picks a theme with a live preview', async () => {
    await alice.page.getByRole('button', { name: 'Upload banner' }).click();
    const dialog = alice.page.getByRole('dialog');
    await dialog.getByTestId('banner-file-input').setInputFiles({ name: 'banner.png', mimeType: 'image/png', buffer: testPng(1800, 600, [20, 90, 80], [10, 30, 50]) });
    await dialog.getByRole('button', { name: 'Save banner' }).click();
    await expect(alice.page.getByText('Banner updated.')).toBeVisible();

    await alice.page.getByTestId('theme-emerald').click();
    const preview = alice.page.getByTestId('appearance-preview');
    await expect.poll(() => preview.evaluate((element) => getComputedStyle(element).getPropertyValue('--akeno-accent').trim())).not.toBe('');
    await alice.page.getByRole('button', { name: 'Save appearance' }).click();
    await expect(alice.page.getByText('Appearance saved.')).toBeVisible();
    await alice.page.screenshot({ path: `${SHOTS}/settings-profile.png`, fullPage: true });
  });

  await test.step('the administrator creates a game with artwork and grants Alice a badge', async () => {
    await admin.page.goto('/admin');
    await admin.page.getByRole('tab', { name: 'Games' }).click();
    await admin.page.getByRole('button', { name: 'New game' }).click();
    await admin.page.getByLabel('Official name').fill(GAME.name);
    await admin.page.getByRole('dialog').getByRole('button', { name: 'Create game' }).click();
    const editor = admin.page.getByRole('dialog');
    await expect(editor).toContainText('Square icon');
    await editor.getByLabel('Add a title ID').fill(GAME.titleId);
    await editor.getByRole('button', { name: 'Map title' }).click();
    await expect(editor).toContainText(GAME.titleId);
    await editor.getByTestId('artwork-input-icon').setInputFiles({ name: 'icon.png', mimeType: 'image/png', buffer: testPng(512, 512, [120, 40, 200], [250, 200, 60]) });
    await expect(admin.page.getByText('Square icon saved.')).toBeVisible();
    await editor.getByTestId('artwork-input-cover').setInputFiles({ name: 'cover.png', mimeType: 'image/png', buffer: testPng(600, 900, [30, 30, 90], [200, 80, 120]) });
    await expect(admin.page.getByText('Vertical cover (2:3) saved.')).toBeVisible();
    await editor.getByLabel('Source').selectOption('own_capture');
    await editor.getByLabel('Attribution').fill('E2E generated test artwork');
    await editor.getByRole('button', { name: 'Save details' }).click();
    await expect(admin.page.getByText('Game saved.')).toBeVisible();
    await admin.page.screenshot({ path: `${SHOTS}/admin-game-editor.png` });
    await editor.getByRole('button', { name: 'Close' }).first().click();

    await admin.page.getByRole('tab', { name: 'Members' }).click();
    const row = admin.page.locator('li').filter({ hasText: '@alice' }).first();
    await row.getByRole('button', { name: 'Badges' }).click();
    const badges = admin.page.getByRole('dialog');
    await badges.getByLabel('Grant badge').selectOption('ps5_alpha_tester');
    await badges.getByRole('button', { name: 'Grant' }).click();
    await expect(admin.page.getByText('Badge granted.')).toBeVisible();
    await expect(badges).toContainText('PS5 Alpha Tester');
  });

  await test.step('Alice features her badge (she cannot feature badges she does not have)', async () => {
    const refused = await apiCall(alice.page, 'PATCH', '/profile/appearance', { featuredBadges: ['founder'] });
    expect(refused.status).toBe(400);
    await alice.page.goto('/settings');
    await alice.page.getByLabel(/PS5 Alpha Tester/).check();
    await alice.page.getByRole('button', { name: 'Save appearance' }).click();
    await expect(alice.page.getByText('Appearance saved.')).toBeVisible();
  });

  await test.step("Alice's console reports the game; Bob sees its artwork in his friend list and on her profile", async () => {
    const console_ = await playwrightRequest.newContext({ baseURL });
    const started = await console_.post('/api/v1/devices/pairings', { data: { deviceName: 'Test PS5', platform: 'PlayStation 5 (homebrew)', clientVersion: '0.3.0' } });
    const pairing = (await started.json()) as { deviceCode: string; userCode: string };
    expect((await apiCall(alice.page, 'POST', '/devices/pairings/approve', { userCode: pairing.userCode })).status).toBe(204);
    const redeemed = await console_.post('/api/v1/devices/pairings/token', { data: { deviceCode: pairing.deviceCode } });
    const auth = { Authorization: `Bearer ${((await redeemed.json()) as { accessToken: string }).accessToken}` };
    const beat = await console_.post('/api/v1/activity/heartbeat', { headers: auth, data: { titleId: GAME.titleId, titleName: 'AURORA DRIFT', platform: 'PS5' } });
    expect(beat.status()).toBe(200);

    await bob.page.goto('/friends?tab=all');
    const entry = bob.page.getByTestId('friend-item').filter({ hasText: 'Alice' });
    await expect(entry.getByTestId('game-activity-line')).toContainText(GAME.name);
    await expect(entry.getByTestId('game-activity-line').locator('img[src^="/media/"]')).toHaveCount(1);
    // Alice's uploaded picture is shown in Bob's list too.
    await expect(entry.locator('img[src^="/media/"]').first()).toBeVisible();

    await bob.page.goto('/u/alice');
    const page = bob.page.getByTestId('profile-page');
    await expect(page).toHaveAttribute('data-theme-id', 'emerald');
    await expect(bob.page.getByTestId('profile-banner').locator('img')).toBeVisible();
    await expect(bob.page.getByTestId('profile-badges')).toContainText('PS5 Alpha Tester');
    await expect(bob.page.getByTestId('game-activity-card')).toContainText(GAME.name);
    await expect(bob.page.getByTestId('game-activity-card').locator('img[src^="/media/"]').first()).toBeVisible();
    const cover = bob.page.getByTestId('recent-game').first().locator('[data-art-variant="cover"] img');
    await expect(cover.first()).toBeVisible();
    // Covers keep their 2:3 shape.
    const box = await bob.page.getByTestId('recent-game').first().locator('[data-art-variant="cover"]').boundingBox();
    expect(box && Math.abs(box.height / box.width - 1.5)).toBeLessThan(0.05);
    await bob.page.screenshot({ path: `${SHOTS}/profile-desktop.png`, fullPage: true });
    await console_.dispose();
  });

  await test.step('the profile works on a phone without horizontal scrolling', async () => {
    const phone = await openUser(browser, true);
    expect(await signIn(phone.page, 'bob')).toBe(true);
    await phone.page.goto('/u/alice');
    await expect(phone.page.getByTestId('profile-display-name')).toBeVisible();
    await noHorizontalOverflow(phone.page);
    await phone.page.screenshot({ path: `${SHOTS}/profile-mobile.png`, fullPage: true });
    await phone.page.goto('/');
    await noHorizontalOverflow(phone.page);
    await phone.page.screenshot({ path: `${SHOTS}/home-mobile.png`, fullPage: true });
    expect(phone.errors).toEqual([]);
    await phone.close();
  });

  await test.step('removing the picture brings the preset avatar back', async () => {
    await alice.page.goto('/settings');
    await alice.page.getByRole('button', { name: 'Remove' }).first().click();
    await alice.page.getByRole('dialog').getByRole('button', { name: 'Remove' }).click();
    await expect(alice.page.getByText(/preset avatar is shown again/)).toBeVisible();
    const me = await apiCall(alice.page, 'GET', '/users/me');
    expect(me.json.profile.avatarImage).toBeNull();
  });

  await test.step('no browser errors or CSP violations occurred', async () => {
    expect(admin.errors).toEqual([]);
    expect(alice.errors).toEqual([]);
    expect(bob.errors).toEqual([]);
  });

  await admin.close();
  await alice.close();
  await bob.close();
});
