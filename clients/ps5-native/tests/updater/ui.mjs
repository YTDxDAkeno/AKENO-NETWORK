#!/usr/bin/env node
// The updater in the real app (PC build, headless SDL): Settings -> Updates, the automatic check
// and its "update available" question, download progress, install, restart, the manual check,
// a damaged download and an incompatible release, against the mock GitHub of mock-github.mjs.
//
//   node clients/ps5-native/tests/updater/ui.mjs <path to akeno-native>
//
// AKN_IT_OUT=<dir> keeps the screenshots. A PC test only; it says nothing about the PS5.
import { spawn } from 'node:child_process';
import { createHash, generateKeyPairSync } from 'node:crypto';
import { copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { buildRelease } from '../../tools/make-update.mjs';
import { startMock } from './mock-github.mjs';

const app = process.argv[2];
if (!app) {
  console.error('usage: ui.mjs <akeno-native>');
  process.exit(2);
}
const here = dirname(fileURLToPath(import.meta.url));
let checks = 0;
let failures = 0;
function check(cond, message) {
  checks++;
  if (!cond) {
    failures++;
    console.error(`FAIL: ${message}`);
  }
}

const work = mkdtempSync(join(tmpdir(), 'akn-updater-ui-'));
const shots = join(work, 'shots');
mkdirSync(shots);
const { publicKey, privateKey } = generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
const keyFile = join(work, 'public.pem');
writeFileSync(keyFile, publicKey.export({ type: 'spki', format: 'pem' }));
const priv = privateKey.export({ type: 'pkcs8', format: 'pem' });

function programBytes(seed, size) {
  const parts = [];
  for (let i = 0; parts.length * 32 < size; i++) parts.push(createHash('sha256').update(`${seed}/${i}`).digest());
  return Buffer.concat(parts).subarray(0, size);
}

function release(version, requires = {}) {
  return buildRelease({
    version,
    channel: version.includes('-') ? 'beta' : 'stable',
    files: [
      { path: 'eboot.elf', data: programBytes(version, 600 * 1024) },
      { path: 'homebrew.js', data: Buffer.from('// homebrew\n') },
    ],
    key: priv,
    requires,
    notes: '',
  });
}

function newRoot(name) {
  const root = join(work, name, 'AKENONative');
  mkdirSync(root, { recursive: true });
  writeFileSync(join(root, 'eboot.elf'), 'factory');
  copyFileSync(join(here, '../../package/homebrew.js'), join(root, 'homebrew.js'));
  return root;
}

// Runs the app; "sync <label>" steps run actions[label], then release the app.
function run(env, args, timeoutMs, actions = {}) {
  return new Promise((resolve) => {
    const child = spawn(app, args, {
      env: { ...process.env, SDL_VIDEODRIVER: 'dummy', SDL_AUDIODRIVER: 'dummy', AKENO_NATIVE_WINDOW: '1920x1080', ...env },
      stdio: ['ignore', 'pipe', 'pipe'],
    });
    let output = '';
    let pending = '';
    child.stdout.on('data', (d) => {
      output += d;
      pending += d;
      let nl;
      while ((nl = pending.indexOf('\n')) >= 0) {
        const m = /^AKN-SYNC ([A-Za-z0-9_-]+)$/.exec(pending.slice(0, nl));
        pending = pending.slice(nl + 1);
        if (m) {
          if (actions[m[1]]) actions[m[1]]();
          writeFileSync(join(env.AKENO_NATIVE_SYNC_DIR, m[1]), 'ok');
        }
      }
    });
    child.stderr.on('data', (d) => (output += d));
    const timer = setTimeout(() => child.kill('SIGKILL'), timeoutMs);
    child.on('close', (code) => {
      clearTimeout(timer);
      resolve({ code, output });
    });
  });
}

const toSettingsUpdates = ['wait 300', ...Array(6).fill('press right'), 'press cross', 'expect screen settings', ...Array(9).fill('press down'),
  'press cross', 'expect screen updates', 'expect text Current version', 'expect text 1.2.0-beta.1'];

async function main() {
  const mock = await startMock();
  try {
    const base = {
      AKENO_NATIVE_UPDATE_API: mock.api,
      AKENO_NATIVE_UPDATE_PREFIX: mock.prefix,
      AKENO_NATIVE_UPDATE_KEY_FILE: keyFile,
      AKENO_NATIVE_DAEMON_PORT: '1', // no daemon: the check runs once the daemon was asked
      AKENO_NATIVE_AKENO_DIR: join(work, 'akeno-data'),
    };
    const rel = release('1.2.0-beta.2');
    mock.publish(rel, { prerelease: true, body: 'Background voice diagnostics and a faster friends list.' });

    // ---- 1. automatic check at start, the question, install, restart ---------------------------
    const root = newRoot('auto');
    const nativeDir = join(work, 'native-auto');
    let r = await run({ ...base, AKENO_NATIVE_DIR: nativeDir, AKENO_NATIVE_SCRIPT: [
      ...toSettingsUpdates,
      'until text 30000 Update available', 'expect text A new AKENO NETWORK update is available. Install now?', 'expect text Later',
      `shot ${join(shots, 'update-01-question.png')}`,
      'press left', 'press cross', 'expect screen updates',
      'until text 30000 Version 1.2.0-beta.2 is installed', `shot ${join(shots, 'update-02-installed.png')}`,
      'press down', 'expect text Restart to apply', 'press cross', 'until text 3000 Close AKENO now?', 'press left', 'press cross',
    ].join(';') }, ['--launcher', '--install-dir', root], 90000);
    check(r.code === 0, `automatic run exits cleanly (code ${r.code})\n${r.output.slice(-3000)}`);
    const active = existsSync(join(root, 'update/active.json')) ? JSON.parse(readFileSync(join(root, 'update/active.json'), 'utf8')) : null;
    check(active && active.version === '1.2.0-beta.2' && active.state === 'pending', `installed from the app: ${JSON.stringify(active)}`);
    check(existsSync(join(root, 'update/versions/1.2.0-beta.2/eboot.elf')), 'the new ELF is in place');
    check(readFileSync(join(root, 'eboot.elf'), 'utf8') === 'factory', 'the factory ELF is untouched');
    const prefs = JSON.parse(readFileSync(join(nativeDir, 'updater.json'), 'utf8'));
    check(prefs.channel === 'beta' && prefs.auto_check === true && prefs.last_check > 0 && prefs.last_seen === '1.2.0-beta.2', `prefs: ${JSON.stringify(prefs)}`);
    const log = readFileSync(join(nativeDir, 'native.log'), 'utf8');
    check(/update\.install.*installed 1\.2\.0-beta\.2/.test(log), 'the log records the install');
    check(!existsSync(join(work, 'akeno-data')) || readdirSync(join(work, 'akeno-data')).length === 0, 'nothing written to the daemon folder');

    // ---- 2. the new version starts (trial), runs normally and keeps itself ----------------------
    // (the same binary pretends to be the new version: AKN_NATIVE_VERSION differs, so this checks the
    // factory side: a start of the old version while the new one is pending counts as "other")
    r = await run({ ...base, AKENO_NATIVE_DIR: nativeDir, AKENO_NATIVE_SCRIPT: 'wait 500;quit' }, ['--launcher', '--install-dir', root], 30000);
    check(r.code === 0, `start of the previous version (code ${r.code})`);
    check(JSON.parse(readFileSync(join(root, 'update/active.json'), 'utf8')).state === 'pending', 'starting the old version does not confirm the new one');

    // ---- 3. manual check, damaged download, retry ----------------------------------------------
    const root2 = newRoot('manual');
    const nativeDir2 = join(work, 'native-manual');
    mkdirSync(nativeDir2, { recursive: true });
    writeFileSync(join(nativeDir2, 'updater.json'), JSON.stringify({ channel: 'beta', auto_check: false, last_check: 0 }));
    mock.state.fault = 'corrupt';
    mock.state.faultAsset = rel.packageName;
    r = await run({ ...base, AKENO_NATIVE_DIR: nativeDir2, AKENO_NATIVE_SCRIPT: [
      ...toSettingsUpdates, 'expect text Last checked: never',
      'press down', 'press down', 'press cross', 'until text 20000 Version 1.2.0-beta.2 is available',
      'press down', 'press down', 'expect text CHANGES IN 1.2.0-beta.2', 'expect text Background voice diagnostics',
      `shot ${join(shots, 'update-03-changelog.png')}`,
      'press down', 'press down', 'press cross', 'until text 3000 Install the update?', 'press left', 'press cross',
      'until text 30000 does not match the signed checksum', 'expect text try again', `shot ${join(shots, 'update-04-damaged.png')}`,
      'sync fixed', 'press cross', 'until text 3000 Install the update?', 'press left', 'press cross', 'until text 30000 Version 1.2.0-beta.2 is installed',
      'quit',
    ].join(';'), AKENO_NATIVE_SYNC_DIR: work }, ['--launcher', '--install-dir', root2], 120000, {
      fixed: () => {
        mock.state.fault = null; // the network is fine again
        mock.state.faultAsset = null;
      },
    });
    check(r.code === 0, `manual run exits cleanly (code ${r.code})\n${r.output.slice(-3000)}`);
    check(existsSync(join(root2, 'update/versions/1.2.0-beta.2/eboot.elf')), 'the retry installed the update');

    // ---- 4. an incompatible release is shown with its reason -----------------------------------
    const m2 = await startMock();
    m2.publish(release('1.2.0-beta.3', { daemon_protocol_min: 3 }), { prerelease: true });
    r = await run({ ...base, AKENO_NATIVE_UPDATE_API: m2.api, AKENO_NATIVE_UPDATE_PREFIX: m2.prefix, AKENO_NATIVE_DIR: join(work, 'native-incompat'),
      AKENO_NATIVE_SCRIPT: [...toSettingsUpdates, 'until text 30000 The AKENO daemon is not running', 'expect notext Update available',
        `shot ${join(shots, 'update-05-incompatible.png')}`, 'quit'].join(';') }, ['--launcher', '--install-dir', newRoot('incompat')], 60000);
    check(r.code === 0, `incompatible run (code ${r.code})\n${r.output.slice(-3000)}`);
    await m2.close();

    // ---- 5. not started from the launcher: checks work, installing is refused -------------------
    r = await run({ ...base, AKENO_NATIVE_DIR: join(work, 'native-noroot'), AKENO_NATIVE_SCRIPT: [...toSettingsUpdates,
      'until text 30000 not started from its launcher folder', 'expect notext Update available', 'quit'].join(';') },
    ['--launcher'], 60000);
    check(r.code === 0, `run without --install-dir (code ${r.code})\n${r.output.slice(-3000)}`);
  } finally {
    await mock.close();
  }
  if (process.env.AKN_IT_OUT) {
    mkdirSync(process.env.AKN_IT_OUT, { recursive: true });
    for (const f of readdirSync(shots)) copyFileSync(join(shots, f), join(process.env.AKN_IT_OUT, f));
  }
  rmSync(work, { recursive: true, force: true });
  console.log(`updater UI: ${checks} checks, ${failures} failures`);
  process.exit(failures ? 1 : 0);
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
