import { devices, expect, request as playwrightRequest, test, type Browser, type Page } from '@playwright/test';

/**
 * SharePlay against a real LiveKit server (no mocks of the media path): one browser broadcasts,
 * another watches; video frames and game audio are verified on the receiving side from WebRTC
 * statistics and from the decoded picture itself. Player 2 input goes through the real
 * controller link to the host's monitor.
 *
 * TEST-GENERATED MEDIA: the broadcaster's getDisplayMedia() is replaced by a canvas animation
 * and an oscillator (clearly labelled below). This exercises the real WebRTC pipeline; it is NOT
 * PS5 game capture. The controller is a scripted object behind navigator.getGamepads(), not a
 * real gamepad.
 *
 * Needs E2E_LIVEKIT_URL etc. like voice.spec.ts. Skipped otherwise.
 */

test.skip(!process.env.E2E_LIVEKIT_URL, 'E2E_LIVEKIT_URL is not set: no LiveKit server to test SharePlay against');
test.use({ launchOptions: { args: ['--use-fake-ui-for-media-stream', '--use-fake-device-for-media-stream', '--autoplay-policy=no-user-gesture-required'] } });

const PASSWORD = 'correct-horse-battery-staple';
const ADMIN_PASSWORD = process.env.E2E_ADMIN_PASSWORD ?? 'e2e-admin-password-123';
const RUN = Date.now().toString(36).slice(-4);
const WAIT = { timeout: 30_000 };

interface TestPad {
  id: string;
  index: number;
  mapping: string;
  connected: boolean;
  timestamp: number;
  axes: number[];
  buttons: Array<{ pressed: boolean; touched: boolean; value: number }>;
}

declare global {
  interface Window {
    __akenoPeerConnections: RTCPeerConnection[];
    __akenoCaptures: MediaStreamTrack[];
    __denyCapture: boolean;
    __testPad: TestPad;
  }
}

/**
 * Test instrumentation, injected before the app loads: records peer connections and captured
 * tracks, replaces screen capture with TEST-GENERATED video (a canvas whose background cycles
 * red → green → blue every 400 ms with a moving bar) plus a 330 Hz tone, and provides a scripted
 * standard-layout controller.
 */
function instrument(): void {
  window.__akenoPeerConnections = [];
  window.__akenoCaptures = [];
  window.__denyCapture = false;
  const Native = window.RTCPeerConnection;
  window.RTCPeerConnection = class extends Native {
    constructor(...args: ConstructorParameters<typeof RTCPeerConnection>) {
      super(...args);
      window.__akenoPeerConnections.push(this);
    }
  } as typeof RTCPeerConnection;

  MediaDevices.prototype.getDisplayMedia = async function () {
    if (window.__denyCapture) throw new DOMException('Permission denied by test', 'NotAllowedError');
    const canvas = document.createElement('canvas');
    canvas.width = 1280;
    canvas.height = 720;
    const g = canvas.getContext('2d')!;
    const colors = ['#ff0000', '#00ff00', '#0000ff'];
    let frame = 0;
    const draw = () => {
      frame += 1;
      g.fillStyle = colors[Math.floor(Date.now() / 400) % 3]!;
      g.fillRect(0, 0, canvas.width, canvas.height);
      g.fillStyle = '#ffffff';
      g.fillRect((frame * 12) % canvas.width, 0, 40, 60);
      g.font = '40px sans-serif';
      g.fillText('TEST-GENERATED VIDEO - not PS5 capture', 40, 680);
    };
    draw();
    const timer = setInterval(draw, 33);
    const stream = canvas.captureStream(30);
    const audioContext = new AudioContext();
    const oscillator = audioContext.createOscillator();
    oscillator.frequency.value = 330;
    const destination = audioContext.createMediaStreamDestination();
    oscillator.connect(destination);
    oscillator.start();
    const audio = destination.stream.getAudioTracks()[0]!;
    stream.addTrack(audio);
    const video = stream.getVideoTracks()[0]!;
    video.addEventListener('ended', () => clearInterval(timer));
    window.__akenoCaptures.push(video, audio);
    return stream;
  };

  const buttons = Array.from({ length: 17 }, () => ({ pressed: false, touched: false, value: 0 }));
  window.__testPad = { id: 'Xbox 360 Controller (XInput STANDARD GAMEPAD)', index: 0, mapping: 'standard', connected: true, timestamp: 0, axes: [0, 0, 0, 0], buttons };
  Navigator.prototype.getGamepads = function () {
    return [window.__testPad as unknown as Gamepad, null, null, null];
  };
}

interface Browserish {
  page: Page;
  errors: string[];
  close: () => Promise<void>;
}

const consoleLogs: string[] = [];
const LIVEKIT_TEARDOWN = /^DataChannel error on \w+: User-Initiated Abort|^publisher data channel '\w+' closed unexpectedly/;

function watchErrors(page: Page, errors: string[]): void {
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => {
    consoleLogs.push(`${new Date().toISOString()} [${message.type()}] ${message.text().slice(0, 300)}`);
    if (consoleLogs.length > 400) consoleLogs.shift();
    if (message.type() === 'error' && !message.text().startsWith('Failed to load resource') && !LIVEKIT_TEARDOWN.test(message.text())) errors.push(message.text());
  });
}

// eslint-disable-next-line no-empty-pattern
test.afterEach(async ({}, testInfo) => {
  if (testInfo.status !== testInfo.expectedStatus) console.log(`Browser console (last ${consoleLogs.length} lines):\n${consoleLogs.join('\n')}`);
});

async function open(browser: Browser, phone = false): Promise<Browserish> {
  const context = await browser.newContext({ ...(phone ? devices['Pixel 7'] : { viewport: { width: 1366, height: 900 } }), permissions: ['microphone', 'camera'] });
  await context.addInitScript(instrument);
  const page = await context.newPage();
  const errors: string[] = [];
  watchErrors(page, errors);
  return { page, errors, close: () => context.close() };
}

async function createInvitationCode(baseURL: string): Promise<string> {
  const api = await playwrightRequest.newContext({ baseURL, extraHTTPHeaders: { Origin: baseURL } });
  const login = await api.post('/api/v1/auth/login', { data: { onlineId: 'e2e_admin', password: ADMIN_PASSWORD } });
  expect(login.ok()).toBe(true);
  const { csrfToken } = (await login.json()) as { csrfToken: string };
  const created = await api.post('/api/v1/admin/invitations', { headers: { 'X-CSRF-Token': csrfToken }, data: { maxUses: 1, expiresInHours: 1, note: `shareplay spec ${RUN}` } });
  expect(created.status()).toBe(201);
  const { code } = (await created.json()) as { code: string };
  await api.dispose();
  return code;
}

async function signIn(page: Page, onlineId: string): Promise<boolean> {
  await page.goto('/login');
  await page.getByLabel('Online ID').fill(onlineId);
  await page.getByLabel('Password').fill(PASSWORD);
  await page.getByRole('button', { name: 'Sign in' }).click();
  return Promise.race([page.waitForURL((url) => !url.pathname.startsWith('/login')).then(() => true), page.getByRole('alert').waitFor().then(() => false)]);
}

/** Reuses the suite's accounts (registration is rate-limited per IP); registers them when run alone. */
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

async function apiCall(page: Page, method: string, path: string, body?: unknown): Promise<{ status: number; json: any }> {
  return page.evaluate(
    async ({ method, path, body }) => {
      const session = await fetch('/api/v1/auth/session').then((response) => response.json());
      const response = await fetch(`/api/v1${path}`, { method, headers: { 'Content-Type': 'application/json', 'X-CSRF-Token': session.csrfToken }, body: body === undefined ? undefined : JSON.stringify(body) });
      const text = await response.text();
      return { status: response.status, json: text ? JSON.parse(text) : null };
    },
    { method, path, body },
  );
}

async function befriend(a: Page, b: Page, bId: string): Promise<void> {
  const friends = await apiCall(a, 'GET', '/friends');
  if (friends.json.friends.some((friend: { id: string }) => friend.id === bId)) return;
  const sent = await apiCall(a, 'POST', '/friends/requests', { userId: bId });
  expect([200, 201]).toContain(sent.status);
  if (sent.json.status === 'pending' && sent.json.request) expect((await apiCall(b, 'POST', `/friends/requests/${sent.json.request.id}/accept`)).status).toBe(200);
}

/**
 * Receive-side verification of the picture: decoded frames advance, and sampling the decoded
 * image shows the test pattern's strong primary colours, changing over time.
 */
async function sampleVideo(page: Page): Promise<{ width: number; height: number; framesSeen: number; dominant: string[] }> {
  return page.evaluate(async () => {
    const video = document.querySelector<HTMLVideoElement>('[data-testid="shareplay-video"]');
    if (!video) return { width: 0, height: 0, framesSeen: 0, dominant: [] };
    let framesSeen = 0;
    const count = () => {
      framesSeen += 1;
      video.requestVideoFrameCallback(count);
    };
    video.requestVideoFrameCallback(count);
    const canvas = document.createElement('canvas');
    canvas.width = 32;
    canvas.height = 18;
    const g = canvas.getContext('2d', { willReadFrequently: true })!;
    const dominant: string[] = [];
    for (let i = 0; i < 12; i += 1) {
      await new Promise((resolve) => setTimeout(resolve, 200));
      g.drawImage(video, 0, 0, 32, 18);
      const [r, gr, b] = g.getImageData(16, 12, 1, 1).data as unknown as number[];
      const max = Math.max(r!, gr!, b!);
      dominant.push(max < 120 ? 'dark' : r === max ? 'red' : gr === max ? 'green' : 'blue');
    }
    return { width: video.videoWidth, height: video.videoHeight, framesSeen, dominant };
  });
}

async function inbound(page: Page, kind: 'audio' | 'video'): Promise<{ bytes: number; energy: number }> {
  return page.evaluate(async (kind) => {
    let bytes = 0;
    let energy = 0;
    for (const pc of window.__akenoPeerConnections) {
      if (pc.connectionState === 'closed') continue;
      const stats = await pc.getStats().catch(() => null);
      stats?.forEach((report: { type: string; kind?: string; bytesReceived?: number; totalAudioEnergy?: number }) => {
        if (report.type === 'inbound-rtp' && report.kind === kind) {
          bytes += report.bytesReceived ?? 0;
          energy += report.totalAudioEnergy ?? 0;
        }
      });
    }
    return { bytes, energy };
  }, kind);
}

async function liveCaptures(page: Page): Promise<number> {
  return page.evaluate(() => window.__akenoCaptures.filter((track) => track.readyState === 'live').length);
}

async function openPeerConnections(page: Page): Promise<number> {
  return page.evaluate(() => window.__akenoPeerConnections.filter((pc) => pc.connectionState !== 'closed').length);
}

async function setPad(page: Page, pressed: number[], axes: number[]): Promise<void> {
  await page.evaluate(
    ({ pressed, axes }) => {
      window.__testPad.buttons.forEach((button, index) => {
        button.pressed = pressed.includes(index);
        button.value = button.pressed ? 1 : 0;
      });
      window.__testPad.axes = axes;
      window.__testPad.timestamp += 1;
    },
    { pressed, axes },
  );
}

async function startSharing(page: Page): Promise<void> {
  await page.getByTestId('shareplay-start-button').click();
  await expect(page.getByRole('dialog')).toContainText('single window');
  await page.getByRole('button', { name: 'Choose what to share' }).click();
}

test('SharePlay: stream, watch with game audio, Player 2 input, stop — real LiveKit', async ({ browser, baseURL }) => {
  test.setTimeout(360_000);
  const host = await open(browser);
  const guest = await open(browser);
  let hostId = '';
  let guestId = '';
  let hostName = '';
  let guestName = '';
  let partyId = '';

  await test.step('a party with two members; SharePlay is offered, nothing is captured yet', async () => {
    ({ id: hostId, displayName: hostName } = await ensureUser(host.page, baseURL!, 'alice', 'Alice'));
    ({ id: guestId, displayName: guestName } = await ensureUser(guest.page, baseURL!, 'bob', 'Bob'));
    await befriend(host.page, guest.page, guestId);
    const party = await apiCall(host.page, 'POST', '/parties', { name: 'SharePlay Check', privacy: 'friends' });
    expect(party.status).toBe(201);
    partyId = party.json.id;
    expect((await apiCall(guest.page, 'POST', `/parties/${partyId}/join`)).status).toBe(200);
    await host.page.goto('/parties');
    await guest.page.goto('/parties');
    await expect(host.page.getByTestId('shareplay-start')).toBeVisible();
    await expect(guest.page.getByTestId('shareplay-start')).toBeVisible();
    expect(await liveCaptures(host.page)).toBe(0);
    // PS5-native capture is shown as unsupported and cannot be started.
    await host.page.getByTestId('shareplay-source').selectOption('ps5-native');
    await expect(host.page.getByTestId('shareplay-ps5-native')).toContainText('not possible yet');
    await expect(host.page.getByTestId('shareplay-start-button')).toBeDisabled();
    await host.page.getByTestId('shareplay-source').selectOption('screen');
  });

  await test.step('a refused capture streams nothing', async () => {
    await host.page.evaluate(() => {
      window.__denyCapture = true;
    });
    await startSharing(host.page);
    await expect(host.page.getByTestId('shareplay-error')).toContainText('Sharing was cancelled');
    expect((await apiCall(host.page, 'GET', `/parties/${partyId}/shareplay`)).json.session).toBeNull();
    await host.page.evaluate(() => {
      window.__denyCapture = false;
    });
  });

  await test.step('the host starts SharePlay; the capture indicator is shown', async () => {
    await startSharing(host.page);
    await expect(host.page.getByTestId('shareplay-capture-indicator')).toBeVisible(WAIT);
    await expect(host.page.getByTestId('shareplay-capture-indicator')).toContainText('with sound');
    await expect(host.page.getByTestId('shareplay-panel')).toHaveAttribute('data-shareplay', 'live', WAIT);
    await expect(host.page.getByTestId('shareplay-bar').first()).toBeVisible();
    await expect(guest.page.getByTestId('shareplay-summary')).toContainText(hostName, WAIT);
  });

  await test.step('the guest watches: real decoded frames and game audio arrive', async () => {
    await guest.page.getByTestId('shareplay-watch').click();
    await expect(guest.page.getByTestId('shareplay-viewer')).toHaveAttribute('data-stream-status', 'live', WAIT);
    await expect.poll(async () => (await sampleVideo(guest.page)).width, WAIT).toBeGreaterThan(0);
    // Congestion control starts low and ramps up: wait for a steady stream (> 15 frames in 2.4 s).
    let sample = await sampleVideo(guest.page);
    await expect.poll(async () => (sample = await sampleVideo(guest.page)).framesSeen, WAIT).toBeGreaterThan(15);
    expect(sample.width).toBeGreaterThanOrEqual(320);
    // The test pattern's colours come through the encoder and decoder, and they change.
    expect(new Set(sample.dominant.filter((colour) => colour !== 'dark')).size).toBeGreaterThanOrEqual(2);
    await expect.poll(async () => (await inbound(guest.page, 'audio')).energy, WAIT).toBeGreaterThan(0);
    await expect(guest.page.getByTestId('shareplay-receive-stats')).toContainText('fps', WAIT);
    await expect(host.page.getByTestId('shareplay-viewer-count')).toContainText('1/', WAIT);
    await expect(host.page.getByTestId('shareplay-send-stats')).toContainText('×', WAIT);
  });

  await test.step('player controls: mute, volume, full screen', async () => {
    await guest.page.getByTestId('shareplay-mute').click();
    expect(await guest.page.evaluate(() => document.querySelector<HTMLAudioElement>('audio[data-akeno-shareplay]')?.muted)).toBe(true);
    await guest.page.getByTestId('shareplay-mute').click();
    expect(await guest.page.evaluate(() => document.querySelector<HTMLAudioElement>('audio[data-akeno-shareplay]')?.muted)).toBe(false);
    await guest.page.getByLabel('Stream volume').fill('0.4');
    expect(await guest.page.evaluate(() => document.querySelector<HTMLAudioElement>('audio[data-akeno-shareplay]')?.volume)).toBeCloseTo(0.4);
    await guest.page.getByTestId('shareplay-fullscreen').click();
    await expect.poll(() => guest.page.evaluate(() => document.fullscreenElement !== null)).toBe(true);
    await guest.page.getByTestId('shareplay-fullscreen').click();
    await expect.poll(() => guest.page.evaluate(() => document.fullscreenElement === null)).toBe(true);
  });

  await test.step('voice keeps working next to SharePlay', async () => {
    await host.page.getByTestId('join-voice').click();
    await guest.page.getByTestId('join-voice').click();
    await expect(host.page.getByTestId('party-voice')).toHaveAttribute('data-voice-status', 'connected', WAIT);
    await expect(guest.page.getByTestId('party-voice')).toHaveAttribute('data-voice-status', 'connected', WAIT);
    // Two rooms, two connections each: leaving the stream must not touch voice.
    await guest.page.getByTestId('shareplay-leave').click();
    await expect(guest.page.getByTestId('shareplay-watch')).toBeVisible();
    await expect(guest.page.getByTestId('party-voice')).toHaveAttribute('data-voice-status', 'connected');
    await guest.page.getByTestId('shareplay-watch').click();
    await expect(guest.page.getByTestId('shareplay-viewer')).toHaveAttribute('data-stream-status', 'live', WAIT);
  });

  await test.step('Player 2: request, explicit approval, input reaches the host, revocation', async () => {
    await expect(guest.page.getByTestId('player2-request')).toBeEnabled(WAIT);
    await guest.page.getByTestId('player2-request').click();
    await expect(guest.page.getByTestId('player2-pending')).toBeVisible();
    await expect(host.page.getByTestId('player2-request-prompt')).toContainText(`${guestName} wants to join as Player 2.`, WAIT);
    await host.page.getByTestId('player2-allow').click();
    await expect(host.page.getByTestId('player2-host-grant')).toContainText(`Player 2 control granted to ${guestName}.`, WAIT);
    await expect(guest.page.getByTestId('player2-granted')).toBeVisible(WAIT);
    await guest.page.getByTestId('player2-use-controller').click();
    await expect(guest.page.getByTestId('player2-granted')).toHaveAttribute('data-sender-status', 'sending', WAIT);

    await setPad(guest.page, [0, 5], [1, 0, 0, -0.5]); // A + R1, left stick right, right stick up
    const monitor = host.page.getByTestId('player2-monitor-state');
    await expect(monitor).toHaveAttribute('data-pressed', 'south r1', WAIT);
    const axes = (await monitor.getAttribute('data-axes'))!.split(',').map(Number);
    expect(axes[0]).toBe(32767);
    expect(axes[3]).toBeLessThan(-10000);
    await expect(host.page.getByTestId('player2-host-grant')).toHaveAttribute('data-input-connected', 'true', WAIT);

    // The PS button (16) is never sent.
    await setPad(guest.page, [16, 2], [0, 0, 0, 0]);
    await expect(monitor).toHaveAttribute('data-pressed', 'west', WAIT);

    // A held button survives (heartbeats) …
    await host.page.waitForTimeout(1200);
    await expect(monitor).toHaveAttribute('data-pressed', 'west');
    // … until the host revokes: everything is released at once and the guest stops sending.
    await host.page.getByTestId('player2-revoke').click();
    await expect(monitor).toHaveCount(0, WAIT);
    await expect(guest.page.getByTestId('player2-granted')).toHaveCount(0, WAIT);
    await expect(guest.page.getByTestId('player2-request')).toBeVisible(WAIT);
    await setPad(guest.page, [], [0, 0, 0, 0]);
  });

  await test.step('the stream continues while the guest browses other pages', async () => {
    await guest.page.getByRole('link', { name: 'Friends' }).first().click();
    await expect(guest.page.getByTestId('shareplay-bar').first()).toContainText('Watching SharePlay');
    // Game audio keeps playing; the video subscription pauses while no player is visible
    // (LiveKit adaptive stream saves the bandwidth) and resumes on the party page.
    const before = (await inbound(guest.page, 'audio')).bytes;
    await guest.page.waitForTimeout(1500);
    expect((await inbound(guest.page, 'audio')).bytes).toBeGreaterThan(before);
    await guest.page.getByRole('link', { name: 'Parties' }).first().click();
    await expect.poll(async () => (await sampleVideo(guest.page)).framesSeen, WAIT).toBeGreaterThan(5);
  });

  await test.step('changing the quality reaches the viewer', async () => {
    await host.page.getByTestId('shareplay-host').getByLabel('Quality').selectOption('480p30');
    await expect(guest.page.getByTestId('shareplay-summary')).toContainText('480p', WAIT);
    await expect.poll(async () => (await sampleVideo(guest.page)).width, { timeout: 45_000 }).toBeLessThanOrEqual(854);
  });

  await test.step('stopping ends capture at once and the viewer sees the end', async () => {
    await host.page.getByTestId('shareplay-stop').click();
    await expect.poll(() => liveCaptures(host.page)).toBe(0);
    await expect(guest.page.getByTestId('shareplay-notice')).toContainText('ended', WAIT);
    await expect(guest.page.getByTestId('shareplay-start')).toBeVisible(WAIT);
    await expect(host.page.getByTestId('shareplay-bar')).toHaveCount(0);
    // Only the voice room's connection stays open (one per browser).
    await expect.poll(() => openPeerConnections(guest.page), WAIT).toBeLessThanOrEqual(2);
    expect((await apiCall(host.page, 'GET', `/parties/${partyId}/shareplay`)).json.session).toBeNull();
  });

  await test.step('publisher disconnect: a reload stops capture; the viewer waits, the host can stop', async () => {
    await startSharing(host.page);
    await expect(host.page.getByTestId('shareplay-panel')).toHaveAttribute('data-shareplay', 'live', WAIT);
    await guest.page.getByTestId('shareplay-watch').click();
    await expect(guest.page.getByTestId('shareplay-viewer')).toHaveAttribute('data-stream-status', 'live', WAIT);
    await host.page.reload();
    await expect(guest.page.getByTestId('shareplay-panel')).toHaveAttribute('data-shareplay', 'host-reconnecting', WAIT);
    await expect(host.page.getByTestId('shareplay-host')).toContainText('not capturing', WAIT);
    await host.page.getByTestId('shareplay-stop').click();
    await expect(guest.page.getByTestId('shareplay-start')).toBeVisible(WAIT);
  });

  await test.step('a phone can watch without horizontal overflow', async () => {
    const phone = await open(browser, true);
    await ensureUser(phone.page, baseURL!, 'bob', 'Bob');
    await phone.page.goto('/parties');
    await startSharing(host.page);
    await phone.page.getByTestId('shareplay-watch').click();
    await expect(phone.page.getByTestId('shareplay-viewer')).toHaveAttribute('data-stream-status', 'live', WAIT);
    await expect.poll(async () => (await sampleVideo(phone.page)).framesSeen, WAIT).toBeGreaterThan(5);
    expect(await phone.page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth + 1)).toBe(true);
    // The phone took the viewer connection over from the desktop tab (one per account).
    await host.page.getByTestId('shareplay-stop').click();
    await phone.close();
  });

  expect(host.errors, host.errors.join('\n')).toEqual([]);
  expect(guest.errors.filter((error) => !/SharePlay moved to another tab/.test(error)), guest.errors.join('\n')).toEqual([]);
  await apiCall(host.page, 'POST', `/parties/${partyId}/close`);
  void hostId;
  await host.close();
  await guest.close();
});
