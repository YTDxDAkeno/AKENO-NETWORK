#!/usr/bin/env node
// End-to-end updater test: the real updater engine (tools/update_cli.c, the same C code as on the
// console) against a mock GitHub (mock-github.mjs) with releases built and signed by
// tools/make-update.mjs, plus the launcher's version choice (package/homebrew.js).
//
//   node clients/ps5-native/tests/updater/run.mjs <path to akn-update>
//
// Covers: up to date, stable and beta channels, download through a redirect, install side by side,
// trial starts, automatic rollback, confirmation, corrupt / truncated / stalled downloads, foreign
// redirects, rate limits, bad and replayed signatures, hostile archives, incompatible firmware and
// daemon, foreign repositories, no key, and an install killed half-way. A PC test only.
import { spawn } from 'node:child_process';
import { createHash, generateKeyPairSync } from 'node:crypto';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';

import { buildRelease, buildZip } from '../../tools/make-update.mjs';
import { startMock } from './mock-github.mjs';

const cli = process.argv[2];
if (!cli) {
  console.error('usage: run.mjs <akn-update>');
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

const work = mkdtempSync(join(tmpdir(), 'akn-updater-'));
const keyPair = () => generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
const official = keyPair();
const other = keyPair();
const pubPath = join(work, 'public.pem');
writeFileSync(pubPath, official.publicKey.export({ type: 'spki', format: 'pem' }));
const privPem = official.privateKey.export({ type: 'pkcs8', format: 'pem' });
const otherPem = other.privateKey.export({ type: 'pkcs8', format: 'pem' });
const sha = (b) => createHash('sha256').update(b).digest('hex');

// The launcher's choice, from the real homebrew.js.
const hbSource = readFileSync(join(here, '../../package/homebrew.js'), 'utf8');
const hb = { module: { exports: {} } };
vm.runInNewContext(hbSource, hb);
const { akenoChoose } = hb.module.exports;

// Program-like bytes (an ELF does not compress 200:1, which the ZIP reader refuses as a bomb).
function programBytes(seed, size) {
  const parts = [];
  for (let i = 0; parts.length * 32 < size; i++) parts.push(createHash('sha256').update(`${seed}/${i}`).digest());
  return Buffer.concat([Buffer.from('\x7fELF'), ...parts]).subarray(0, size);
}

function files(version) {
  return [
    { path: 'eboot.elf', data: programBytes(version, 96 * 1024) },
    { path: 'homebrew.js', data: Buffer.from(`// homebrew ${version}\n`) },
    { path: 'sce_sys/icon0.png', data: Buffer.from(`icon ${version}`) },
  ];
}

function release(version, opts = {}) {
  return buildRelease({
    version,
    channel: opts.channel ?? (version.includes('-') ? 'beta' : 'stable'),
    files: opts.files ?? files(version),
    key: opts.key ?? privPem,
    requires: opts.requires ?? { firmware_min: '12.00', daemon_protocol_min: 1 },
    notes: `What's new in ${version}`,
    manifestOverride: opts.manifestOverride,
    zipOverride: opts.zipOverride,
  });
}

function newRoot(name) {
  const root = join(work, name, 'AKENONative');
  mkdirSync(join(root, 'sce_sys'), { recursive: true });
  writeFileSync(join(root, 'eboot.elf'), 'factory build 1.1.0');
  writeFileSync(join(root, 'homebrew.js'), hbSource);
  return root;
}

function run(args, { killAfterMs } = {}) {
  return new Promise((resolve) => {
    const child = spawn(cli, args, { stdio: ['ignore', 'pipe', 'pipe'] });
    let out = '';
    let err = '';
    child.stdout.on('data', (d) => (out += d));
    child.stderr.on('data', (d) => (err += d));
    let timer = null;
    if (killAfterMs) timer = setTimeout(() => child.kill('SIGKILL'), killAfterMs);
    child.on('close', (code, signal) => {
      clearTimeout(timer);
      const kv = {};
      for (const line of out.split('\n')) {
        const eq = line.indexOf('=');
        if (eq > 0 && !line.startsWith('progress=')) kv[line.slice(0, eq)] = line.slice(eq + 1);
      }
      kv.progress = out.split('\n').filter((l) => l.startsWith('progress=')).map((l) => l.slice(9));
      resolve({ code, signal, out, err, kv });
    });
  });
}

function common(mock, root, extra = []) {
  return ['--api', mock.api, '--prefix', mock.prefix, '--host', '127.0.0.1', '--host', 'localhost', '--key', pubPath,
    '--firmware', '12.20', '--daemon-protocol', '1', ...(root ? ['--root', root] : []), ...extra];
}

const readActive = (root) => (existsSync(join(root, 'update/active.json')) ? JSON.parse(readFileSync(join(root, 'update/active.json'), 'utf8')) : null);
const listVersions = (root) => (existsSync(join(root, 'update/versions')) ? readdirSync(join(root, 'update/versions')).sort() : []);
const history = (root) => (existsSync(join(root, 'update/history.log')) ? readFileSync(join(root, 'update/history.log'), 'utf8') : '');

async function main() {
  const mock = await startMock();
  try {
    // ---- 1. nothing published -----------------------------------------------------------------
    const root = newRoot('main');
    const factorySha = sha(readFileSync(join(root, 'eboot.elf')));
    let r = await run(['check', ...common(mock, root, ['--current', '1.1.0'])]);
    check(r.kv.result === 'up-to-date', `empty list: ${r.out}`);

    // ---- 2. a stable release: check, install through the redirect -------------------------------
    const r120 = release('1.2.0');
    mock.publish(r120);
    r = await run(['check', ...common(mock, root, ['--current', '1.1.0'])]);
    check(r.kv.result === 'available' && r.kv.version === '1.2.0', `1.2.0 available: ${r.out}`);
    check(/KB|MB/.test(r.kv.size ?? ''), `size shown: ${r.kv.size}`);
    check((r.kv.notes ?? '').includes('Changes'), 'release notes shown');
    mock.state.requests.length = 0;
    r = await run(['install', ...common(mock, root, ['--current', '1.1.0'])]);
    check(r.kv.install === 'ok', `install 1.2.0: ${r.out} ${r.err}`);
    check(mock.state.requests.some((l) => l.includes(' localhost:') && l.endsWith('/assets/native-1.2.0/akeno-native-1.2.0.zip')),
      'the package came through the redirect to the asset host');
    check(['download', 'verify', 'unpack', 'install'].every((p) => r.kv.progress.some((l) => l.startsWith(p))), `progress phases: ${r.kv.progress.slice(-4)}`);
    const dl = r.kv.progress.filter((l) => l.startsWith('download')).map((l) => l.split(' ').map(Number));
    check(dl.length > 0 && dl[dl.length - 1][1] === r120.zip.length && dl[dl.length - 1][2] === r120.zip.length, 'download progress reaches the full size');
    for (const f of files('1.2.0')) {
      const p = join(root, 'update/versions/1.2.0', f.path);
      check(existsSync(p) && sha(readFileSync(p)) === sha(f.data), `installed ${f.path} matches`);
    }
    check(sha(readFileSync(join(root, 'eboot.elf'))) === factorySha, 'the factory eboot.elf is untouched');
    check(!existsSync(join(root, 'update/staging')), 'staging removed after install');
    let a = readActive(root);
    check(a && a.version === '1.2.0' && a.state === 'pending' && a.attempts === 0, `active pending: ${JSON.stringify(a)}`);
    check(a && a.previous.elf === 'eboot.elf' && a.previous.version === '', 'falls back to the factory version');
    check(history(root).includes('installed 1.2.0'), 'history records the install');
    check(akenoChoose(a).elf === 'update/versions/1.2.0/eboot.elf', 'launcher starts the new version');

    // ---- 3. the new version never confirms: two trial starts, then the launcher falls back -------
    r = await run(['boot', '--root', root, '--current', '1.2.0']);
    check(r.kv.boot === 'trial' && r.kv.attempt === '1', `trial 1: ${r.out}`);
    check(akenoChoose(readActive(root)).elf === 'update/versions/1.2.0/eboot.elf', 'second start still tries the new version');
    r = await run(['boot', '--root', root, '--current', '1.2.0']);
    check(r.kv.boot === 'trial' && r.kv.attempt === '2', `trial 2: ${r.out}`);
    const plan = akenoChoose(readActive(root));
    check(plan.elf === 'eboot.elf' && plan.note.includes('did not start'), `launcher falls back: ${JSON.stringify(plan)}`);
    r = await run(['boot', '--root', root, '--current', '1.1.0']);
    check(r.kv.boot === 'rolled-back' && r.kv.failed_version === '1.2.0', `rollback recorded: ${r.out}`);
    a = readActive(root);
    check(a.version === '1.1.0' && a.elf === 'eboot.elf' && a.state === 'confirmed' && a.rejected.includes('1.2.0'), `after rollback: ${JSON.stringify(a)}`);
    check(!listVersions(root).includes('1.2.0'), 'the failed version was deleted');
    check(history(root).includes('rolled back from 1.2.0'), 'history records the rollback');
    check(akenoChoose(a).elf === 'eboot.elf', 'launcher starts the factory version after the rollback');
    r = await run(['check', ...common(mock, root, ['--current', '1.1.0'])]);
    check(r.kv.result === 'available' && r.kv.previously_failed === '1', `the failed version is flagged: ${r.out}`);

    // ---- 4. install again and confirm ----------------------------------------------------------
    r = await run(['install', ...common(mock, root, ['--current', '1.1.0'])]);
    check(r.kv.install === 'ok', `reinstall: ${r.out}`);
    check(!readActive(root).rejected.includes('1.2.0'), 'reinstalling clears the rejection');
    r = await run(['boot', '--root', root, '--current', '1.2.0']);
    check(r.kv.boot === 'trial' && r.kv.own_elf === 'update/versions/1.2.0/eboot.elf', `trial: ${r.out}`);
    r = await run(['confirm', '--root', root, '--current', '1.2.0']);
    check(r.kv.confirmed === '1', 'confirmed');
    a = readActive(root);
    check(a.state === 'confirmed' && a.attempts === 0, `confirmed state: ${JSON.stringify(a)}`);
    r = await run(['boot', '--root', root, '--current', '1.2.0']);
    check(r.kv.boot === 'normal', 'later starts are normal');
    check(akenoChoose(a).elf === 'update/versions/1.2.0/eboot.elf', 'launcher keeps the confirmed version');
    r = await run(['check', ...common(mock, root, ['--current', '1.2.0', '--own-elf', 'update/versions/1.2.0/eboot.elf'])]);
    check(r.kv.result === 'up-to-date', `1.2.0 is up to date: ${r.out}`);

    // ---- 5. beta channel ------------------------------------------------------------------------
    const rBeta = release('1.3.0-beta.1');
    mock.publish(rBeta, { prerelease: true });
    r = await run(['check', ...common(mock, root, ['--current', '1.2.0'])]);
    check(r.kv.result === 'up-to-date', `stable ignores the beta: ${r.out}`);
    r = await run(['install', ...common(mock, root, ['--current', '1.2.0', '--channel', 'beta', '--own-elf', 'update/versions/1.2.0/eboot.elf'])]);
    check(r.kv.version === '1.3.0-beta.1' && r.kv.prerelease === '1' && r.kv.install === 'ok', `beta install: ${r.out}`);
    a = readActive(root);
    check(a.version === '1.3.0-beta.1' && a.previous.version === '1.2.0' && a.previous.elf === 'update/versions/1.2.0/eboot.elf', `beta active: ${JSON.stringify(a)}`);
    check(JSON.stringify(listVersions(root)) === JSON.stringify(['1.2.0', '1.3.0-beta.1']), `kept versions: ${listVersions(root)}`);
    const back = akenoChoose({ ...a, attempts: 2 });
    check(back.elf === 'update/versions/1.2.0/eboot.elf', 'a failing beta falls back to 1.2.0, not the factory version');

    // the launcher entry itself, as websrv runs it (window.workingDir, ApiClient.fsGetFileText)
    {
      const ctx = {
        window: { workingDir: root },
        ApiClient: {
          fsGetFileText: async (p) => (existsSync(p) ? { status: 200, data: readFileSync(p, 'utf8') } : { status: 404, data: null }),
        },
        module: { exports: {} },
      };
      vm.runInNewContext(hbSource, ctx);
      const entry = await ctx.module.exports.main();
      check(entry.mainText === 'AKENO NETWORK' && entry.secondaryText.includes('1.3.0-beta.1'), `launcher text: ${entry.secondaryText}`);
      const launch = await entry.onclick();
      check(launch.path === `${root}/update/versions/1.3.0-beta.1/eboot.elf`, `launch path: ${launch.path}`);
      check(JSON.stringify(launch.args) === JSON.stringify(['--launcher', '--install-dir', root]), `launch args: ${launch.args}`);
      check(!launch.daemon, 'AKENO Native starts as the foreground app, not as a background payload');
      const texts = (entry.options ?? []).map((o) => o.text);
      check(texts.some((t) => t.includes('previous version (1.2.0)')) && texts.some((t) => t.includes('installed by hand')), `options: ${texts}`);
      const prevLaunch = await entry.options[0].onclick();
      check(prevLaunch.path === `${root}/update/versions/1.2.0/eboot.elf`, `previous: ${prevLaunch.path}`);
      for (const bad of [{ format: 1, state: 'confirmed', elf: '../../evil.elf' }, { format: 1, state: 'confirmed', elf: '/data/x.elf' },
        { format: 2, state: 'confirmed', elf: 'update/versions/1.2.0/eboot.elf' }, { format: 1, state: 'weird', elf: 'update/versions/1.2.0/eboot.elf' },
        { format: 1, state: 'pending', attempts: 5, elf: 'update/versions/1.2.0/eboot.elf', previous: { elf: '../x' } }]) {
        check(akenoChoose(bad).elf === 'eboot.elf', `unexpected active.json falls back to eboot.elf: ${JSON.stringify(bad)}`);
      }
      const fresh = { ...ctx, window: { workingDir: newRoot('fresh') }, module: { exports: {} } };
      vm.runInNewContext(hbSource, fresh);
      const fentry = await fresh.module.exports.main();
      check((await fentry.onclick()).path.endsWith('/AKENONative/eboot.elf') && !fentry.options, 'no updates yet: eboot.elf, no options');
    }

    // ---- 6. network and download faults: nothing changes ----------------------------------------
    const faults = [
      ['corrupt', 'akeno-native-1.2.0.zip', /signed checksum/],
      ['truncate', 'akeno-native-1.2.0.zip', /./],
      ['stall', 'akeno-native-1.2.0.zip', /./],
      ['redirect-foreign', 'akeno-native-1.2.0.zip', /127\.0\.0\.2/],
    ];
    for (const [fault, asset, why] of faults) {
      const froot = newRoot(`fault-${fault}`);
      mock.state.fault = fault;
      mock.state.faultAsset = asset;
      r = await run(['install', ...common(mock, froot, ['--current', '1.1.0', '--timeout-ms', '1500'])]);
      check(r.kv.install === 'failed' && why.test(r.kv.install_why ?? ''), `${fault}: ${r.kv.install_why} ${r.kv.why}`);
      check(readActive(froot) === null && listVersions(froot).length === 0, `${fault}: nothing installed`);
      check(!existsSync(join(froot, 'update/staging')), `${fault}: staging cleaned`);
    }
    mock.state.fault = 'chunked';
    mock.state.faultAsset = 'akeno-native-1.2.0.zip';
    r = await run(['install', ...common(mock, newRoot('chunked'), ['--current', '1.1.0'])]);
    check(r.kv.install === 'ok', `chunked transfer: ${r.out}`);
    mock.state.faultAsset = null;
    for (const [fault, why] of [['rate-limit', /rate limit|limit/i], ['http-500', /500|GitHub/]]) {
      mock.state.fault = fault;
      r = await run(['check', ...common(mock, root, ['--current', '1.1.0'])]);
      check(r.kv.result === 'failed' && why.test(r.kv.why), `${fault}: ${r.kv.why}`);
    }
    mock.state.fault = 'redirect-foreign';
    mock.state.faultAsset = 'akeno-native-update.json';
    r = await run(['check', ...common(mock, root, ['--current', '1.1.0'])]);
    check(r.kv.result === 'failed' && /not an official GitHub address/.test(r.kv.why), `foreign redirect for the manifest: ${r.kv.why}`);
    mock.state.fault = null;
    mock.state.faultAsset = null;

    // ---- 7. an install killed half-way leaves the running version untouched ---------------------
    const kroot = newRoot('killed');
    mock.state.fault = 'stall';
    mock.state.faultAsset = 'akeno-native-1.2.0.zip';
    r = await run(['install', ...common(mock, kroot, ['--current', '1.1.0', '--timeout-ms', '20000'])], { killAfterMs: 1500 });
    check(r.signal === 'SIGKILL', 'the install was killed');
    check(existsSync(join(kroot, 'update/staging')), 'the killed install left its staging folder');
    mkdirSync(join(kroot, 'update/versions/1.2.0.tmp'), { recursive: true });
    writeFileSync(join(kroot, 'update/versions/1.2.0.tmp/eboot.elf'), 'half');
    mock.state.fault = null;
    r = await run(['boot', '--root', kroot, '--current', '1.1.0']);
    check(r.kv.boot === 'normal', `boot after the kill: ${r.out}`);
    check(!existsSync(join(kroot, 'update/staging')) && !existsSync(join(kroot, 'update/versions/1.2.0.tmp')), 'leftovers removed at the next start');
    check(readActive(kroot) === null && akenoChoose(null).elf === 'eboot.elf', 'still the factory version');

    // ---- 8. signatures, replays, hostile archives, compatibility, foreign assets ----------------
    const cases = [
      ['signed with another key', release('1.4.0', { key: otherPem }), {}, 'rejected', /signature/i],
      ['manifest changed after signing', (() => {
        const x = release('1.4.0');
        x.manifestText = Buffer.from(x.manifestText.toString().replace('"stable"', '"beta"  '));
        return x;
      })(), {}, 'rejected', /signature/i],
      ['manifest of another release', release('1.4.0', { manifestOverride: { tag: 'native-1.3.9', version: '1.3.9' } }), {}, 'rejected', /belongs to/],
      ['stable channel for a pre-release', release('1.4.0-rc.1', { channel: 'stable' }), { prerelease: true }, 'rejected', /pre-release/],
      ['firmware too old', release('1.4.0', { requires: { firmware_min: '13.00' } }), {}, 'incompatible', /firmware/i],
      ['daemon too old', release('1.4.0', { requires: { daemon_protocol_min: 2 } }), {}, 'incompatible', /daemon/i],
      ['needs a newer installed version', release('1.4.0', { requires: { min_installed_version: '1.3.0' } }), {}, 'incompatible', /1\.3\.0/],
    ];
    for (const [name, rel, pub, expect, why] of cases) {
      const m = await startMock();
      m.publish(rel, pub);
      r = await run(['check', ...common(m, newRoot(`case-${name.replace(/\W+/g, '-')}`), ['--current', '1.2.0', '--channel', 'beta'])]);
      check(r.kv.result === expect && why.test(r.kv.why), `${name}: ${r.kv.result} ${r.kv.why}`);
      await m.close();
    }
    {
      const m = await startMock();
      m.publish(release('1.4.0'), { urlRepo: 'someone/fork' });
      r = await run(['check', ...common(m, root, ['--current', '1.2.0'])]);
      check(r.kv.result === 'up-to-date' && r.kv.skipped === '1', `assets outside the official repository are skipped: ${r.out}`);
      r = await run(['check', '--api', m.api, '--prefix', m.prefix, '--host', '127.0.0.1', '--host', 'localhost', '--current', '1.2.0']);
      await m.close();
      const m2 = await startMock();
      m2.publish(release('1.4.0'));
      r = await run(['check', '--api', m2.api, '--prefix', m2.prefix, '--host', '127.0.0.1', '--host', 'localhost', '--current', '1.2.0']);
      check(r.kv.result === 'unverified', `no key: ${r.kv.result} ${r.kv.why}`);
      await m2.close();
    }
    // hostile archives: a path outside the folder, an unlisted extra file, a symbolic link
    const hostile = [
      ['path traversal', (fs) => buildZip([...fs.map((f) => ({ name: f.path, data: f.data })), { name: '../evil.elf', data: Buffer.from('x') }]), /evil|name|unsafe/i],
      ['unlisted file', (fs) => buildZip([...fs.map((f) => ({ name: f.path, data: f.data })), { name: 'payload.sh', data: Buffer.from('#!/bin/sh') }]), /payload\.sh|does not list/],
      ['symbolic link', (fs) => buildZip(fs.map((f) => ({ name: f.path, data: f.data, externalAttr: f.path === 'homebrew.js' ? (0o120777 << 16) >>> 0 : undefined }))), /link|special/i],
      ['bad CRC', (fs) => buildZip(fs.map((f) => ({ name: f.path, data: f.data, crc: f.path === 'eboot.elf' ? 1 : undefined }))), /CRC|damaged|checksum/i],
    ];
    for (const [name, make, why] of hostile) {
      const m = await startMock();
      const fs = files('1.4.0');
      m.publish(release('1.4.0', { files: fs, zipOverride: make(fs) }));
      const hroot = newRoot(`hostile-${name.replace(/\W+/g, '-')}`);
      r = await run(['install', ...common(m, hroot, ['--current', '1.2.0'])]);
      check(r.kv.install === 'failed' && why.test(r.kv.install_why ?? ''), `${name}: ${r.kv.install_why}`);
      check(readActive(hroot) === null && !existsSync(join(hroot, '..', 'evil.elf')) && !existsSync(join(hroot, 'update', 'evil.elf')), `${name}: nothing written`);
      await m.close();
    }
  } finally {
    await mock.close();
  }
  rmSync(work, { recursive: true, force: true });
  console.log(`updater e2e: ${checks} checks, ${failures} failures`);
  process.exit(failures ? 1 : 0);
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
