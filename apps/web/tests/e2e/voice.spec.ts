import { devices, expect, request as playwrightRequest, test, type Browser, type Page } from '@playwright/test';

/**
 * Party voice against a real LiveKit server (no mocks): two browsers with Chromium's fake
 * microphone join a party's voice chat, hear each other (WebRTC inbound audio with energy),
 * see speaking and mute state, and lose voice access when the leader removes them. Also checks
 * that leaving releases the microphone and that a second tab takes the voice connection over.
 *
 * Needs a LiveKit server and the AKENO server configured for it, for example:
 *   E2E_LIVEKIT_URL=ws://127.0.0.1:7880 E2E_LIVEKIT_API_KEY=… E2E_LIVEKIT_API_SECRET=… pnpm test:e2e
 * (see docs/TESTING.md). Skipped otherwise.
 */

test.skip(!process.env.E2E_LIVEKIT_URL, 'E2E_LIVEKIT_URL is not set: no LiveKit server to test voice against');
test.use({ launchOptions: { args: ['--use-fake-ui-for-media-stream', '--use-fake-device-for-media-stream', '--autoplay-policy=no-user-gesture-required'] } });

const PASSWORD = 'correct-horse-battery-staple';
const ADMIN_PASSWORD = process.env.E2E_ADMIN_PASSWORD ?? 'e2e-admin-password-123';
const RUN = Date.now().toString(36).slice(-4);

declare global {
  interface Window {
    __akenoPeerConnections: RTCPeerConnection[];
    __akenoMicTracks: MediaStreamTrack[];
  }
}

async function createInvitationCode(baseURL: string): Promise<string> {
  const api = await playwrightRequest.newContext({ baseURL, extraHTTPHeaders: { Origin: baseURL } });
  const login = await api.post('/api/v1/auth/login', { data: { onlineId: 'e2e_admin', password: ADMIN_PASSWORD } });
  expect(login.ok()).toBe(true);
  const { csrfToken } = (await login.json()) as { csrfToken: string };
  const created = await api.post('/api/v1/admin/invitations', {
    headers: { 'X-CSRF-Token': csrfToken },
    data: { maxUses: 1, expiresInHours: 1, note: `voice spec ${RUN}` },
  });
  expect(created.status()).toBe(201);
  const { code } = (await created.json()) as { code: string };
  await api.dispose();
  return code;
}

/** Test instrumentation only: remembers peer connections and microphone tracks so the test can read WebRTC stats and check that the microphone is released. */
function instrument(): void {
  window.__akenoPeerConnections = [];
  window.__akenoMicTracks = [];
  const Native = window.RTCPeerConnection;
  window.RTCPeerConnection = class extends Native {
    constructor(...args: ConstructorParameters<typeof RTCPeerConnection>) {
      super(...args);
      window.__akenoPeerConnections.push(this);
    }
  } as typeof RTCPeerConnection;
  const getUserMedia = MediaDevices.prototype.getUserMedia;
  MediaDevices.prototype.getUserMedia = async function (constraints) {
    const stream = await getUserMedia.call(this, constraints);
    window.__akenoMicTracks.push(...stream.getAudioTracks());
    return stream;
  };
}

interface Browserish {
  page: Page;
  errors: string[];
  close: () => Promise<void>;
}

function watchErrors(page: Page, errors: string[]): void {
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => {
    if (message.type() === 'error' && !message.text().startsWith('Failed to load resource')) errors.push(message.text());
  });
}

async function openDesktop(browser: Browser, baseURL: string, phone = false): Promise<Browserish> {
  const context = await browser.newContext({ ...(phone ? devices['Pixel 7'] : { viewport: { width: 1366, height: 860 } }), permissions: ['microphone'] });
  await context.grantPermissions(['microphone'], { origin: baseURL });
  await context.addInitScript(instrument);
  const page = await context.newPage();
  const errors: string[] = [];
  watchErrors(page, errors);
  return { page, errors, close: () => context.close() };
}

async function signIn(page: Page, onlineId: string, password = PASSWORD): Promise<boolean> {
  await page.goto('/login');
  await page.getByLabel('Online ID').fill(onlineId);
  await page.getByLabel('Password').fill(password);
  await page.getByRole('button', { name: 'Sign in' }).click();
  return Promise.race([
    page.waitForURL((url) => !url.pathname.startsWith('/login')).then(() => true),
    page.getByRole('alert').waitFor().then(() => false),
  ]);
}

/**
 * Uses the suite's existing accounts (registration is rate-limited per IP and the whole suite
 * shares one address); registers them when this spec runs on its own.
 */
async function ensureUser(page: Page, baseURL: string, onlineId: string, displayName: string): Promise<{ id: string; displayName: string }> {
  if (!(await signIn(page, onlineId))) {
    await page.goto('/register');
    await page.getByLabel('Invitation code').fill(await createInvitationCode(baseURL));
    await page.getByLabel('Online ID').fill(onlineId);
    await page.getByLabel('Display name').fill(displayName);
    await page.getByLabel('Password', { exact: true }).fill(PASSWORD);
    await page.getByLabel('Confirm password').fill(PASSWORD);
    await page.getByRole('button', { name: 'Create account' }).click();
  }
  await expect(page.getByTestId('welcome-name')).toBeVisible();
  const me = (await apiCall(page, 'GET', '/users/me')).json as { id: string; profile: { displayName: string } };
  return { id: me.id, displayName: me.profile.displayName };
}

async function befriend(a: Page, b: Page, bId: string): Promise<void> {
  const friends = await apiCall(a, 'GET', '/friends');
  if (friends.json.friends.some((friend: { id: string }) => friend.id === bId)) return;
  const sent = await apiCall(a, 'POST', '/friends/requests', { userId: bId });
  expect([200, 201]).toContain(sent.status);
  if (sent.json.status === 'pending' && sent.json.request) expect((await apiCall(b, 'POST', `/friends/requests/${sent.json.request.id}/accept`)).status).toBe(200);
}

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

/** Received audio over all open peer connections: RTP bytes and decoded audio energy. */
async function inboundAudio(page: Page): Promise<{ bytes: number; energy: number }> {
  return page.evaluate(async () => {
    let bytes = 0;
    let energy = 0;
    for (const pc of window.__akenoPeerConnections) {
      if (pc.connectionState === 'closed') continue;
      const stats = await pc.getStats().catch(() => null);
      stats?.forEach((report: { type: string; kind?: string; bytesReceived?: number; totalAudioEnergy?: number }) => {
        if (report.type === 'inbound-rtp' && report.kind === 'audio') {
          bytes += report.bytesReceived ?? 0;
          energy += report.totalAudioEnergy ?? 0;
        }
      });
    }
    return { bytes, energy };
  });
}

async function liveMicTracks(page: Page): Promise<number> {
  return page.evaluate(() => window.__akenoMicTracks.filter((track) => track.readyState === 'live').length);
}

async function openPeerConnections(page: Page): Promise<number> {
  return page.evaluate(() => window.__akenoPeerConnections.filter((pc) => pc.connectionState !== 'closed').length);
}

function member(page: Page, userId: string) {
  return page.getByTestId('party-members').locator(`[data-user-id="${userId}"]`);
}

test('party voice with a real LiveKit server', async ({ browser, baseURL }) => {
  const ana = await openDesktop(browser, baseURL!);
  const ben = await openDesktop(browser, baseURL!);
  let anaId = '';
  let benId = '';
  let anaName = '';
  let benName = '';

  await test.step('two friends are in a party; voice is offered', async () => {
    ({ id: anaId, displayName: anaName } = await ensureUser(ana.page, baseURL!, 'alice', 'Alice'));
    ({ id: benId, displayName: benName } = await ensureUser(ben.page, baseURL!, 'bob', 'Bob'));
    await befriend(ana.page, ben.page, benId);
    const party = await apiCall(ana.page, 'POST', '/parties', { name: 'Voice Check', privacy: 'friends' });
    expect(party.status).toBe(201);
    expect((await apiCall(ben.page, 'POST', `/parties/${party.json.id}/join`)).status).toBe(200);

    await ana.page.goto('/parties');
    await ben.page.goto('/parties');
    await expect(ana.page.getByTestId('party-members').getByTestId('party-member')).toHaveCount(2);
    await expect(ana.page.getByTestId('join-voice')).toBeVisible();
    expect(await liveMicTracks(ana.page)).toBe(0); // nothing captured before joining
  });

  await test.step('both join voice and receive each other’s audio', async () => {
    await ana.page.getByTestId('join-voice').click();
    await expect(ana.page.getByTestId('party-voice')).toHaveAttribute('data-voice-status', 'connected');
    await ben.page.getByTestId('join-voice').click();
    await expect(ben.page.getByTestId('party-voice')).toHaveAttribute('data-voice-status', 'connected');
    // The server reports voice membership to the other member too (webhook → party update).
    await expect(member(ana.page, benId).getByTestId('member-voice-state')).toBeVisible();
    await expect(member(ben.page, anaId).getByTestId('member-voice-state')).toBeVisible();

    // Real audio: inbound RTP keeps growing and carries sound (the fake microphone beeps).
    await expect.poll(async () => (await inboundAudio(ben.page)).energy, { timeout: 20_000 }).toBeGreaterThan(0);
    const first = await inboundAudio(ben.page);
    await expect.poll(async () => (await inboundAudio(ben.page)).bytes, { timeout: 10_000 }).toBeGreaterThan(first.bytes);
    await expect.poll(async () => (await inboundAudio(ana.page)).energy, { timeout: 20_000 }).toBeGreaterThan(0);
  });

  await test.step('speaking and mute state come from the room', async () => {
    await expect(member(ben.page, anaId).getByTestId('member-voice-state')).toContainText('Speaking', { timeout: 20_000 });
    await ana.page.getByTestId('toggle-mute').click();
    await expect(ana.page.getByTestId('toggle-mute')).toHaveAttribute('aria-pressed', 'true');
    await expect(member(ben.page, anaId).getByTestId('member-voice-state')).toContainText('muted');
    await ana.page.getByTestId('toggle-mute').click();
    await expect(member(ben.page, anaId).getByTestId('member-voice-state')).not.toContainText('muted');
  });

  await test.step('muting someone for yourself stops receiving their audio', async () => {
    await member(ben.page, anaId).getByRole('button', { name: `Volume of ${anaName}` }).click();
    await ben.page.getByRole('button', { name: 'Mute for me' }).click();
    await ben.page.waitForTimeout(1500); // unsubscription reaches the server
    const muted = await inboundAudio(ben.page);
    await ben.page.waitForTimeout(2000);
    expect((await inboundAudio(ben.page)).bytes).toBe(muted.bytes);
    await ben.page.getByRole('button', { name: 'Unmute for me' }).click();
    await expect.poll(async () => (await inboundAudio(ben.page)).bytes, { timeout: 10_000 }).toBeGreaterThan(muted.bytes);
    await ben.page.keyboard.press('Escape');
    await ben.page.getByTestId('party-name').click(); // closes the volume popover
  });

  await test.step('voice stays connected across page navigation and shows in the sidebar', async () => {
    await ben.page.getByRole('navigation', { name: 'Main' }).getByRole('link', { name: 'Friends' }).click();
    await expect(ben.page.getByTestId('voice-bar').first()).toBeVisible();
    const before = await inboundAudio(ben.page);
    await expect.poll(async () => (await inboundAudio(ben.page)).bytes).toBeGreaterThan(before.bytes);
    await ben.page.getByRole('navigation', { name: 'Main' }).getByRole('link', { name: 'Parties' }).click();
    await expect(ben.page.getByTestId('party-voice')).toHaveAttribute('data-voice-status', 'connected');
  });

  await test.step('a page reload leaves voice; the microphone test shows a live level', async () => {
    // A full page load disconnects from the room cleanly (no lingering participant).
    await ana.page.goto('/settings?tab=voice');
    // Chromium's fake microphone plays a steady beep that noise suppression can treat as noise.
    const suppression = ana.page.getByRole('switch', { name: 'Noise suppression' });
    await suppression.click();
    await expect(suppression).toHaveAttribute('aria-checked', 'false');
    const test = ana.page.getByTestId('mic-test');
    await test.getByRole('button', { name: 'Test microphone' }).click();
    await expect.poll(async () => Number(await test.getByRole('meter', { name: 'Microphone level' }).getAttribute('aria-valuenow')), { timeout: 15_000 }).toBeGreaterThan(0);
    await test.getByRole('button', { name: 'Stop test' }).click();
    await suppression.click();
    await expect.poll(() => liveMicTracks(ana.page)).toBe(0);
    await expect(member(ben.page, anaId).getByTestId('member-voice-state')).toHaveCount(0);
  });

  await test.step('a second tab takes over the voice connection', async () => {
    await ana.page.goto('/parties');
    await ana.page.getByTestId('join-voice').click();
    await expect(ana.page.getByTestId('party-voice')).toHaveAttribute('data-voice-status', 'connected');
    const second = await ana.page.context().newPage();
    const secondErrors: string[] = [];
    watchErrors(second, secondErrors);
    await second.goto('/parties');
    await second.getByTestId('join-voice').click();
    await expect(second.getByTestId('party-voice')).toHaveAttribute('data-voice-status', 'connected');
    await expect(ana.page.getByTestId('voice-error')).toContainText('another tab');
    await expect.poll(() => liveMicTracks(ana.page)).toBe(0);
    await second.getByTestId('leave-voice').click();
    await expect(second.getByTestId('join-voice')).toBeVisible();
    await expect.poll(() => liveMicTracks(second)).toBe(0);
    await expect.poll(() => openPeerConnections(second)).toBe(0);
    expect(secondErrors).toEqual([]);
    await second.close();
  });

  await test.step('removing a member from the party ends their voice access', async () => {
    await ana.page.getByTestId('join-voice').click();
    await expect(ana.page.getByTestId('party-voice')).toHaveAttribute('data-voice-status', 'connected');
    await expect(member(ana.page, benId).getByTestId('member-voice-state')).toBeVisible();
    await member(ana.page, benId).getByRole('button', { name: `Manage ${benName}` }).click();
    await ana.page.getByRole('menuitem', { name: 'Remove from party' }).click();
    await expect(ben.page.getByRole('button', { name: 'Start party' })).toBeVisible();
    await expect.poll(() => liveMicTracks(ben.page)).toBe(0);
    await expect.poll(() => openPeerConnections(ben.page)).toBe(0);
    await expect(ana.page.getByTestId('party-members').getByTestId('party-member')).toHaveCount(1);
    // And the server refuses a new voice token to the removed member.
    const party = await apiCall(ana.page, 'GET', '/parties/current');
    expect((await apiCall(ben.page, 'POST', `/parties/${party.json.party.id}/voice/token`)).status).toBe(403);
  });

  await test.step('on a phone: voice works and the voice bar stays above the bottom navigation', async () => {
    // The administrator account is an ordinary member here.
    const cid = await openDesktop(browser, baseURL!, true);
    expect(await signIn(cid.page, 'e2e_admin', ADMIN_PASSWORD)).toBe(true);
    const cidId = (await apiCall(cid.page, 'GET', '/users/me')).json.id;
    await befriend(ana.page, cid.page, cidId);
    // Joins straight from the invitation: accepting and connecting to voice in one tap.
    const party = await apiCall(ana.page, 'GET', '/parties/current');
    expect((await apiCall(ana.page, 'POST', `/parties/${party.json.party.id}/invitations`, { userId: cidId })).status).toBe(201);
    await cid.page.goto('/');
    const invitation = cid.page.getByTestId('invitation').filter({ hasText: party.json.party.name });
    await invitation.getByRole('button', { name: 'Join voice' }).click();
    await expect(cid.page).toHaveURL(/\/parties$/);
    await expect(cid.page.getByTestId('party-voice')).toHaveAttribute('data-voice-status', 'connected');
    await expect(member(cid.page, anaId).getByTestId('member-voice-state')).toBeVisible();
    await expect.poll(async () => (await inboundAudio(cid.page)).energy, { timeout: 20_000 }).toBeGreaterThan(0);
    await cid.page.getByRole('navigation', { name: 'Quick navigation' }).getByRole('link', { name: 'Friends' }).click();
    const bar = cid.page.getByTestId('voice-bar').last();
    await expect(bar).toBeVisible();
    const overflow = await cid.page.evaluate(() => document.documentElement.scrollWidth - window.innerWidth);
    expect(overflow).toBeLessThanOrEqual(1);
    const barBox = (await bar.boundingBox())!;
    const navBox = (await cid.page.getByRole('navigation', { name: 'Quick navigation' }).boundingBox())!;
    expect(barBox.y + barBox.height).toBeLessThanOrEqual(navBox.y);
    await bar.getByRole('button', { name: 'Leave voice' }).click();
    await expect(cid.page.getByTestId('voice-bar')).toHaveCount(0);
    await expect.poll(() => liveMicTracks(cid.page)).toBe(0);
    expect(cid.errors).toEqual([]);
    await cid.close();
  });

  await test.step('leaving voice releases the microphone, also after repeated joins', async () => {
    for (let cycle = 0; cycle < 3; cycle += 1) {
      await ana.page.getByTestId('leave-voice').click();
      await expect(ana.page.getByTestId('join-voice')).toBeVisible();
      await expect.poll(() => liveMicTracks(ana.page)).toBe(0);
      await expect.poll(() => openPeerConnections(ana.page)).toBe(0);
      if (cycle < 2) {
        await ana.page.getByTestId('join-voice').click();
        await expect(ana.page.getByTestId('party-voice')).toHaveAttribute('data-voice-status', 'connected');
      }
    }
  });

  await test.step('no browser errors or CSP violations occurred', async () => {
    expect(ana.errors).toEqual([]);
    expect(ben.errors).toEqual([]);
  });

  await ana.close();
  await ben.close();
});
