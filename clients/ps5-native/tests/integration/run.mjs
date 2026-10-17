#!/usr/bin/env node
// Native UI integration test for AKENO NETWORK Native (PC build, headless SDL).
//
//   node clients/ps5-native/tests/integration/run.mjs <path to akeno-native>
//
// AKN_IT_OUT=<dir> keeps the screenshots and the first run's report there (CI uploads them).
//
// Runs the real app with SDL's dummy video and audio drivers and drives it through SDL's
// game-controller event path (a virtual controller), against a fake AKENO server and fake AKENO
// daemons on loopback: one like the Alpha 0.5 release (no native interface) and one with the
// native interface of docs/PS5_NATIVE_PHASE2.md (key file, hello, state, event ring, proxy).
// Checks the screens it reaches and the text they show, the diagnostic report, the log, screenshot
// pixels, every request the server and the daemons received (GET only), and that the daemon's
// files are never modified. The end-to-end test with the real server and daemon is tests/e2e.
// This proves the app logic on a PC only; it is not evidence about PS5 hardware.
import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { chmodSync, copyFileSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { createServer } from 'node:http';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { inflateSync } from 'node:zlib';

const app = process.argv[2];
if (!app) {
  console.error('usage: run.mjs <akeno-native>');
  process.exit(2);
}

const TOKEN = 'akd_0123456789abcdefghijklmnopqrstuvwxyzABCDEFG';
let failures = 0;
let checks = 0;
function check(cond, message) {
  checks++;
  if (!cond) {
    failures++;
    console.error(`FAIL: ${message}`);
  }
}

function listen(handler) {
  return new Promise((resolve) => {
    const server = createServer(handler);
    server.listen(0, '127.0.0.1', () => resolve(server));
  });
}

function sha256(path) {
  return createHash('sha256').update(readFileSync(path)).digest('hex');
}

// Decodes the PNGs the app writes (8-bit RGBA, filter 0) into raw pixels.
function readPng(path) {
  const buf = readFileSync(path);
  let pos = 8;
  let width = 0;
  let height = 0;
  const idat = [];
  while (pos < buf.length) {
    const len = buf.readUInt32BE(pos);
    const type = buf.toString('ascii', pos + 4, pos + 8);
    const data = buf.subarray(pos + 8, pos + 8 + len);
    if (type === 'IHDR') {
      width = data.readUInt32BE(0);
      height = data.readUInt32BE(4);
    } else if (type === 'IDAT') idat.push(data);
    pos += 12 + len;
  }
  const raw = inflateSync(Buffer.concat(idat));
  const stride = width * 4 + 1;
  return {
    width,
    height,
    pixel(x, y) {
      const o = y * stride + 1 + x * 4;
      if (raw[y * stride] !== 0) throw new Error('unexpected PNG filter');
      return [raw[o], raw[o + 1], raw[o + 2], raw[o + 3]];
    },
  };
}

function near(rgb, target, tolerance) {
  return Math.abs(rgb[0] - target[0]) <= tolerance && Math.abs(rgb[1] - target[1]) <= tolerance && Math.abs(rgb[2] - target[2]) <= tolerance;
}

function run(env, timeoutMs) {
  return new Promise((resolve) => {
    const child = spawn(app, [], {
      env: { ...process.env, SDL_VIDEODRIVER: 'dummy', SDL_AUDIODRIVER: 'dummy', AKENO_NATIVE_WINDOW: '1920x1080', ...env },
      stdio: ['ignore', 'pipe', 'pipe'],
    });
    let output = '';
    child.stdout.on('data', (d) => (output += d));
    child.stderr.on('data', (d) => (output += d));
    const timer = setTimeout(() => child.kill('SIGKILL'), timeoutMs);
    child.on('exit', (code, signal) => {
      clearTimeout(timer);
      resolve({ code, signal, output });
    });
  });
}

// Runs the app and answers its "sync" steps: the harness acts, then creates the file it waits for.
function runWithSync(env, timeoutMs, actions, dir) {
  return new Promise((resolve) => {
    const child = spawn(app, [], {
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
        const line = pending.slice(0, nl);
        pending = pending.slice(nl + 1);
        const m = /^AKN-SYNC ([A-Za-z0-9_-]+)$/.exec(line);
        if (m) {
          if (actions[m[1]]) actions[m[1]]();
          writeFileSync(join(dir, m[1]), 'ok');
        }
      }
    });
    child.stderr.on('data', (d) => (output += d));
    const timer = setTimeout(() => child.kill('SIGKILL'), timeoutMs);
    child.on('exit', (code, signal) => {
      clearTimeout(timer);
      resolve({ code, signal, output });
    });
  });
}

// Pictures the fake daemon serves through GET /api/media (the server's JPEG "fallback" variant) and
// GET /api/local-art (a console icon, PNG), in solid colours that the screenshots are checked for.
const PICTURES = {
  AVATAR: '/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAMCAgMCAgMDAwMEAwMEBQgFBQQEBQoHBwYIDAoMDAsKCwsNDhIQDQ4RDgsLEBYQERMUFRUVDA8XGBYUGBIUFRT/2wBDAQMEBAUEBQkFBQkUDQsNFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBT/wAARCAEAAQADASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwD56ooor8iP9FQooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigD/2Q==',
  BANNER: '/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAMCAgMCAgMDAwMEAwMEBQgFBQQEBQoHBwYIDAoMDAsKCwsNDhIQDQ4RDgsLEBYQERMUFRUVDA8XGBYUGBIUFRT/2wBDAQMEBAUEBQkFBQkUDQsNFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBT/wAARCAFAA8ADASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwCvRRRX5IfxeFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQB/9k=',
  COVER: '/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAMCAgMCAgMDAwMEAwMEBQgFBQQEBQoHBwYIDAoMDAsKCwsNDhIQDQ4RDgsLEBYQERMUFRUVDA8XGBYUGBIUFRT/2wBDAQMEBAUEBQkFBQkUDQsNFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBT/wgARCAHCASwDASIAAhEBAxEB/8QAFQABAQAAAAAAAAAAAAAAAAAAAAf/xAAWAQEBAQAAAAAAAAAAAAAAAAAABgj/2gAMAwEAAhADEAAAAawM+WoAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAH//EABQQAQAAAAAAAAAAAAAAAAAAALD/2gAIAQEAAQUCB2//xAAUEQEAAAAAAAAAAAAAAAAAAACQ/9oACAEDAQE/AQn/AP/EABQRAQAAAAAAAAAAAAAAAAAAAJD/2gAIAQIBAT8BCf8A/8QAFBABAAAAAAAAAAAAAAAAAAAAsP/aAAgBAQAGPwIHb//EABQQAQAAAAAAAAAAAAAAAAAAALD/2gAIAQEAAT8hB2//2gAMAwEAAgADAAAAEAwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww/wD/xAAUEQEAAAAAAAAAAAAAAAAAAACQ/9oACAEDAQE/EAn/AP/EABQRAQAAAAAAAAAAAAAAAAAAAJD/2gAIAQIBAT8QCf8A/8QAFBABAAAAAAAAAAAAAAAAAAAAsP/aAAgBAQABPxAHb//Z',
  ICON: 'iVBORw0KGgoAAAANSUhEUgAAAIAAAACACAYAAADDPmHLAAABT0lEQVR4nO3SQQEAEADAQKQWWwNieOwuwR6bZ487yFq/A/jLAHEGiDNAnAHiDBBngDgDxBkgzgBxBogzQJwB4gwQZ4A4A8QZIM4AcQaIM0CcAeIMEGeAOAPEGSDOAHEGiDNAnAHiDBBngDgDxBkgzgBxBogzQJwB4gwQZ4A4A8QZIM4AcQaIM0CcAeIMEGeAOAPEGSDOAHEGiDNAnAHiDBBngDgDxBkgzgBxBogzQJwB4gwQZ4A4A8QZIM4AcQaIM0CcAeIMEGeAOAPEGSDOAHEGiDNAnAHiDBBngDgDxBkgzgBxBogzQJwB4gwQZ4A4A8QZIM4AcQaIM0CcAeIMEGeAOAPEGSDOAHEGiDNAnAHiDBBngDgDxBkgzgBxBogzQJwB4gwQZ4A4A8QZIM4AcQaIM0CcAeIMEGeAOAPEGSDOAHEGiDNAnAHiDBBngLgHmcsDZ+KGMKkAAAAASUVORK5CYII=',
  ME: '/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAMCAgMCAgMDAwMEAwMEBQgFBQQEBQoHBwYIDAoMDAsKCwsNDhIQDQ4RDgsLEBYQERMUFRUVDA8XGBYUGBIUFRT/2wBDAQMEBAUEBQkFBQkUDQsNFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBT/wAARCAEAAQADASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwDtaKKK/az+CgooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigD//2Q==',
};
const MEDIA = {
  bobAvatar: 'a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1', bobBanner: 'b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2', astroCover: 'c3c3c3c3c3c3c3c3c3c3c3c3c3c3c3c3',
  meAvatar: 'd4d4d4d4d4d4d4d4d4d4d4d4d4d4d4d4', carolAvatar: 'e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5', danAvatar: 'f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6',
};
const MEDIA_DATA = {
  [MEDIA.bobAvatar]: PICTURES.AVATAR, [MEDIA.bobBanner]: PICTURES.BANNER, [MEDIA.astroCover]: PICTURES.COVER, [MEDIA.meAvatar]: PICTURES.ME,
  [MEDIA.carolAvatar]: Buffer.from('this is not a picture at all').toString('base64'), // corrupt: initials stay
  // danAvatar: not on the server (404): initials stay
};
const ASTRO_ART = { gameId: 'g-astro', icon: null, cover: MEDIA.astroCover, banner: null };

function presence(status, game = null, lastSeenAt = null) {
  return {
    status, lastSeenAt, activity: null,
    game: game ? { titleId: game.titleId, name: game.name, platform: 'PS5', startedAt: new Date(Date.now() - 1200000).toISOString(), art: game.art || null } : null,
  };
}

function friend(id, onlineId, displayName, pres, statusMessage = '', avatarImage = null) {
  return { id, onlineId, displayName, avatar: 'default', avatarImage, deleted: false, friendsSince: '2026-01-01T00:00:00.000Z', statusMessage, presence: pres };
}

// A fake daemon with the native interface: its key file, the allow-listed routes the native app uses
// and a proxy that answers like the AKENO server (shapes of packages/shared/src/contracts).
async function startIpcDaemon(akenoDir) {
  const keyPath = join(akenoDir, 'native.key');
  const newKey = () => createHash('sha256').update(String(Math.random())).digest('hex');
  const state = { key: newKey(), firstKey: '', seq: 5, events: [], requests: [], rejected: 0, media: [], localArt: [] };
  state.firstKey = state.key;
  writeFileSync(keyPath, state.key);
  chmodSync(keyPath, 0o600);
  const friends = [
    friend('u-carol', 'carol', 'Carol', presence('offline', null, new Date(Date.now() - 7200000).toISOString()), '', MEDIA.carolAvatar),
    friend('u-bob', 'bob', 'Bob', presence('online', { titleId: 'PPSA01234', name: 'Astro Bot', art: ASTRO_ART }), 'Ranked tonight', MEDIA.bobAvatar),
    friend('u-ada', 'ada', 'Ada Lovelace', presence('online')),
    friend('u-dan', 'dan', 'Dan', presence('away'), '', MEDIA.danAvatar),
  ];
  for (let i = 1; i <= 8; i++) friends.push(friend(`u-f${i}`, `friend_0${i}`, `Friend ${i}`, presence('offline')));
  const profile = (user, extra) => ({
    user: { id: user.id, onlineId: user.onlineId, displayName: user.displayName, avatar: 'default', avatarImage: user.avatarImage || null, deleted: false },
    presence: user.presence, joinedAt: '2026-03-01T10:00:00.000Z', restricted: false, bio: '', statusMessage: user.statusMessage || null,
    favoriteGames: [], favoriteGameDetails: [], relationship: { state: 'friend', friendRequestId: null, canMessage: true, canSendFriendRequest: false },
    mutualFriendCount: 1, mutualFriends: [], appearance: { theme: 'default', accentColor: null, bannerImage: null, bannerOverlay: false },
    badges: [], featuredBadges: [], party: null, ...extra,
  });
  const me = { id: 'u1', onlineId: 'akeno_tester', displayName: 'Akeno Tester', presence: presence('online'), statusMessage: '', avatarImage: MEDIA.meAvatar };
  const activity = (games) => ({
    current: null, historyVisible: true, playtimeVisible: true, recentSessions: [], trackedSince: '2026-01-01T00:00:00.000Z',
    recentGames: games.map((g) => ({ titleId: g[0], name: g[1], platform: 'PS5', art: g[3] || null, lastPlayedAt: new Date(Date.now() - 86400000).toISOString(),
      lastSessionSeconds: 3600, totalSeconds: g[2], sessionCount: 3, playingNow: false })),
    totals: { seconds: games.reduce((s, g) => s + g[2], 0), games: games.length, sessions: 3 * games.length },
  });
  const proxy = (res, data) => json(res, 200, { ok: true, status: 200, data });
  function json(res, status, body) {
    res.writeHead(status, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(body));
  }
  const server = await listen((req, res) => {
    const key = req.headers['x-akeno-native-key'];
    state.requests.push({ method: req.method, path: req.url, key: !!key, origin: req.headers.origin });
    if (req.method === 'GET' && req.url === '/api/instance') return json(res, 200, { app: 'akeno-network-console', version: '0.5.0-alpha', nativeIpc: 1 });
    if (key !== state.key) {
      state.rejected++;
      return json(res, 403, { ok: false, error: { code: 'NATIVE_KEY_INVALID', message: 'This native key is not valid.' } });
    }
    const url = new URL(req.url, 'http://x');
    if (url.pathname === '/api/native/hello')
      return json(res, 200, { ok: true, ipc: 1, app: 'akeno-network-console', version: '0.5.0-alpha', build: 'fake-ps5-native-ipc', paired: true,
        onlineId: 'akeno_tester', realtime: 'connected', eventsNext: state.seq });
    if (url.pathname === '/api/state')
      return json(res, 200, { ok: true, paired: true, onlineId: 'akeno_tester', realtime: { state: 'connected', detail: '', secondsInState: 9 },
        activity: { enabled: true, state: 'playing', detail: '', titleId: 'CUSA00001', name: 'Test Game', startedAt: null, sinceSeconds: 600 },
        inbox: { latestSeq: 0, seenSeq: 0, unseen: 0, latest: null }, eventsNext: state.seq });
    if (url.pathname === '/api/events') {
      const after = Number(url.searchParams.get('after') || 0);
      return json(res, 200, { ok: true, next: state.seq, gap: false, events: state.events.filter((e) => e.seq > after) });
    }
    if (url.pathname === '/api/media') {
      const id = url.searchParams.get('id');
      state.media.push({ id, variant: url.searchParams.get('v') });
      if (!MEDIA_DATA[id]) return json(res, 404, { ok: false, error: { code: 'NOT_FOUND', message: 'The image could not be loaded.' } });
      return json(res, 200, { ok: true, type: 'image/jpeg', data: MEDIA_DATA[id] });
    }
    if (url.pathname === '/api/local-art') {
      const title = url.searchParams.get('title');
      state.localArt.push(title);
      if (title !== 'CUSA00001') return json(res, 404, { ok: false, error: { code: 'NOT_FOUND', message: 'No icon for this title on this console.' } });
      return json(res, 200, { ok: true, type: 'image/png', source: '/system_data/priv/appmeta/CUSA00001/icon0.png', data: PICTURES.ICON });
    }
    if (url.pathname === '/api/v1/friends') return proxy(res, { friends });
    if (url.pathname === '/api/v1/friends/requests') return proxy(res, { incoming: [{ id: 'r1' }], outgoing: [] });
    if (url.pathname === '/api/v1/users/akeno_tester') return proxy(res, profile(me, { relationship: { state: 'self' } }));
    if (url.pathname === '/api/v1/users/akeno_tester/activity') return proxy(res, activity([['PPSA05555', 'Gran Turismo 7', 36000, ASTRO_ART]]));
    const m = /^\/api\/v1\/users\/(u-[a-z0-9]+)(\/activity)?$/.exec(url.pathname);
    const f = m && friends.find((x) => x.id === m[1]);
    if (f && m[2]) return proxy(res, activity([['PPSA01234', 'Astro Bot', 5400]]));
    if (f) return proxy(res, profile(f, f.id === 'u-bob' ? {
      bio: 'Speedrunner.', favoriteGames: ['Astro Bot'], favoriteGameDetails: [{ name: 'Astro Bot', art: ASTRO_ART }],
      party: { id: 'p1', name: 'Friday crew', memberCount: 3, maxMembers: 8, isMember: false },
      appearance: { theme: 'emerald', accentColor: null, bannerImage: MEDIA.bobBanner, bannerOverlay: false },
      badges: [{ id: 'founder', awardedAt: '2026-01-01T00:00:00.000Z' }, { id: 'ps5_alpha_tester', awardedAt: '2026-02-01T00:00:00.000Z' }],
      featuredBadges: ['ps5_alpha_tester'],
    } : {}));
    return json(res, 403, { ok: false, error: { code: 'NOT_ALLOWED_FOR_NATIVE', message: 'no' } });
  });
  return {
    port: server.address().port,
    requests: state.requests,
    media: state.media,
    localArt: state.localArt,
    get key() { return state.key; },
    get firstKey() { return state.firstKey; },
    get rejected() { return state.rejected; },
    push(type, data) {
      state.seq++;
      state.events.push({ seq: state.seq, type, data });
      if (type === 'presence.updated') {
        const f = friends.find((x) => x.id === data.userId);
        if (f) f.presence = data.presence;
      }
    },
    rotateKey() { // a daemon restart: new key, new event counter
      state.key = newKey();
      state.seq = 1;
      state.events = [];
      friends.find((x) => x.id === 'u-dan').presence = presence('online', { titleId: 'PPSA07777', name: 'Kart Racer' });
      writeFileSync(keyPath, state.key);
      chmodSync(keyPath, 0o600);
    },
    close() { server.close(); },
  };
}

const DEVICE_SELF = {
  device: {
    id: 'd1', name: 'Living room PS5', platform: 'ps5', clientVersion: '0.5.0-alpha',
    scopes: ['profile:read', 'friends:read', 'messages:read', 'realtime'],
    createdAt: '2026-10-01T00:00:00.000Z', lastUsedAt: '2026-10-17T00:00:00.000Z', expiresAt: '2027-04-01T00:00:00.000Z',
    ipAddress: '203.0.113.9', current: true,
  },
  user: { id: 'u1', onlineId: 'akeno_tester', displayName: 'Akeno Tester' },
  apiVersion: 1,
  realtimeProtocolVersion: 1,
};

async function main() {
  const root = mkdtempSync(join(tmpdir(), 'akn-it-'));
  const akenoDir = join(root, 'akeno');
  const nativeDir = join(root, 'native');
  mkdirSync(akenoDir, { mode: 0o700 });

  // Fake AKENO server: only GET /api/v1/devices/current with the device token is valid.
  const serverRequests = [];
  let respond401 = false;
  const server = await listen((req, res) => {
    serverRequests.push(`${req.method} ${req.url} ${req.headers.authorization === `Bearer ${TOKEN}` ? 'token' : 'no-token'}${req.headers.upgrade ? ' upgrade' : ''}`);
    if (req.method === 'GET' && req.url === '/api/v1/devices/current' && req.headers.authorization === `Bearer ${TOKEN}`) {
      if (respond401) {
        res.writeHead(401, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: { code: 'UNAUTHENTICATED', message: 'Sign in again.' } }));
        return;
      }
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ ...DEVICE_SELF, serverTime: new Date(Date.now() + 5000).toISOString() }));
      return;
    }
    res.writeHead(404, { 'Content-Type': 'application/json' });
    res.end('{"error":{"code":"NOT_FOUND","message":"Not found."}}');
  });
  // Fake AKENO daemon: the liveness answer of clients/ps5/app/app_routes.c.
  const daemonRequests = [];
  const daemon = await listen((req, res) => {
    daemonRequests.push(`${req.method} ${req.url} host=${req.headers.host} key=${req.headers['x-akeno-native-key'] ? 'yes' : 'no'}`);
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end('{"app":"akeno-network-console","version":"0.5.0-alpha"}');
  });
  const serverUrl = `http://127.0.0.1:${server.address().port}`;
  const settingsPath = join(akenoDir, 'settings.json');
  const credentialPath = join(akenoDir, 'device.json');
  writeFileSync(settingsPath, JSON.stringify({ version: 1, server: serverUrl, deviceName: 'PlayStation 5' }));
  chmodSync(settingsPath, 0o600);
  writeFileSync(credentialPath, JSON.stringify({ version: 1, server: serverUrl, accessToken: TOKEN, deviceId: 'd1', onlineId: 'akeno_tester' }) + '\n');
  chmodSync(credentialPath, 0o600);
  const before = {
    settings: sha256(settingsPath), credential: sha256(credentialPath), credentialMtime: statSync(credentialPath).mtimeMs,
    files: readdirSync(akenoDir).sort().join(','),
  };

  const shots = join(root, 'shots');
  mkdirSync(shots);
  const env = {
    AKENO_NATIVE_AKENO_DIR: akenoDir,
    AKENO_NATIVE_DIR: nativeDir,
    AKENO_NATIVE_DAEMON_PORT: String(daemon.address().port),
  };

  // ---- Run 1: a full session ---------------------------------------------------------------------
  const script = [
    'until account 15000', 'wait 400',
    'expect screen home', 'expect focus friends', 'pad down', 'expect focus controller', 'wait 250', `shot ${shots}/home.png`,
    // spatial navigation with the D-pad and the stick (repeat via hold)
    'pad up', 'expect focus friends', 'pad down', 'expect focus controller',
    'padhold right 430', 'expect focus connection', // a press and one repeat (400 ms)
    'stick -30000 0 300', 'expect focus audio', // the left stick moves like the D-pad
    'pad left', 'expect focus controller',
    // a later Phase 2 tile only explains itself
    'pad up', 'pad right', 'expect focus messages', 'pad cross', 'expect screen home', 'expect text Messages arrives in Phase 2.2',
    'pad left', 'pad down', 'expect focus controller',
    // the release daemon has no native interface: Friends says so and what to do
    'pad up', 'pad cross', 'expect screen friends', 'until ipc 10000', 'wait 300',
    'expect text This AKENO daemon has no native interface', 'expect text akeno-network-ps5-native-ipc.elf', `shot ${shots}/friends-release-daemon.png`,
    'pad circle', 'expect screen home', 'pad down', 'expect focus controller',
    // controller test: every button shows, hold circle to return
    // (SDL's virtual controller has no touchpad mapping on a PC; the PS5 driver maps it as b4)
    'pad cross', 'expect screen controller', 'pad triangle', 'pad square', 'pad l1', 'pad r1', 'pad options', 'press touchpad',
    'padhold cross 400', 'stick -20000 25000 300', 'pad circle', 'expect screen controller', `shot ${shots}/controller.png`,
    'padhold circle 1300', 'expect screen home',
    // audio test, answered "yes"
    'pad right', 'pad cross', 'expect screen audio', 'expect focus audio-play', 'pad cross', 'until audio 10000', 'wait 200',
    'expect dialog heard', 'pad cross', 'expect dialog none', `shot ${shots}/audio.png`,
    // too soon for a second test: refused with a hint, no test runs
    'pad cross', 'wait 300', 'pad circle', 'expect screen home',
    // connection
    'pad right', 'pad cross', 'expect screen connection', 'pad cross', 'until account 15000', 'wait 300', `shot ${shots}/connection.png`, 'pad circle',
    // system: save the report
    'pad right', 'pad cross', 'expect screen system', 'pad cross', 'wait 300', `shot ${shots}/system.png`, 'pad circle',
    // controller unplugged and back: keyboard-equivalent input keeps working
    'unplug', 'wait 300', `shot ${shots}/disconnected.png`, 'press left', 'expect focus connection', 'plug', 'wait 300', 'pad right',
    'expect focus system',
    // exit: circle opens the dialog on Cancel; the Exit tile on Exit
    'pad circle', 'expect dialog exit', 'expect focus exit-cancel', 'pad cross', 'expect dialog none',
    'pad right', 'expect focus exit', 'pad cross', 'expect focus exit-confirm', 'pad cross',
  ].join(';');
  const r1 = await run({ ...env, AKENO_NATIVE_SCRIPT: script }, 120000);
  check(r1.code === 0, `run 1 exits cleanly (code ${r1.code}, signal ${r1.signal})\n${r1.output.slice(-3000)}`);
  const report = readFileSync(join(nativeDir, 'native-report.txt'), 'utf8');
  const log = readFileSync(join(nativeDir, 'native.log'), 'utf8');
  check(report.includes('Signed in as:     Akeno Tester (@akeno_tester)'), 'report: signed-in account');
  check(report.includes('Device name:      Living room PS5'), 'report: device name');
  check(report.includes('Permissions:      4: profile:read, friends:read, messages:read, realtime'), 'report: permissions');
  check(/Clock offset:\s+\+[45] s/.test(report), 'report: clock offset from serverTime');
  check(report.includes('AKENO daemon:     Running 0.5.0-alpha'), 'report: daemon detected');
  check(report.includes('Server:           http to loopback'), 'report: server described, not named');
  check(report.includes('Outcome:          accepted and paced at playback speed'), 'report: audio paced');
  check(report.includes('Heard:            HEARD (your report)'), 'report: heard answer recorded');
  check(report.includes('Tests run:        1'), 'report: the early second test was refused');
  check(/94 calls, 94 succeeded/.test(report), 'report: every output call recorded');
  check(/Controller events:\d{2,}/.test(report), 'report: controller events counted');
  for (const b of ['cross', 'circle', 'square', 'triangle', 'l1', 'r1', 'options', 'touchpad']) {
    check(new RegExp(` ${b}=[1-9]`).test(report), `report: ${b} press counted`);
  }
  check(/Attached\/removed: 2 \/ 1 events/.test(report), 'report: unplug and re-plug seen');
  check(report.includes('Glyph misses:     0'), 'every screen visited, no text lost in the glyph cache');
  check(log.includes('controller.removed'), 'log: controller removal');
  check(log.includes('audio.heard: test 1: HEARD'), 'log: heard answer');
  check(log.includes('ui.screen: controller'), 'log: screens');
  for (const text of [report, log]) {
    check(!text.includes(TOKEN) && !text.includes('akd_0123'), 'no token in report or log');
    check(!text.includes('203.0.113.9'), 'no IP address in report or log');
  }
  check((statSync(join(nativeDir, 'native-report.txt')).mode & 0o077) === 0, 'report is private (0600)');
  check((statSync(join(nativeDir, 'native.log')).mode & 0o077) === 0, 'log is private (0600)');

  // The daemon's data is untouched; the server only saw read-only device-status requests.
  check(sha256(settingsPath) === before.settings, 'settings.json unchanged');
  check(sha256(credentialPath) === before.credential, 'device.json unchanged');
  check(statSync(credentialPath).mtimeMs === before.credentialMtime, 'device.json not rewritten');
  check(readdirSync(akenoDir).sort().join(',') === before.files, 'no file added to the daemon folder');
  check(serverRequests.length >= 2, `server contacted (${serverRequests.length})`);
  check(serverRequests.every((r) => r === 'GET /api/v1/devices/current token'), `only GET /devices/current: ${serverRequests.join(' | ')}`);
  check(daemonRequests.length >= 2 && daemonRequests.every((r) => r === `GET /api/instance host=127.0.0.1:${daemon.address().port} key=no`),
    `release daemon: only the liveness probe, never a key: ${daemonRequests.join(' | ')}`);
  check(report.includes('State:            This AKENO daemon has no native interface: version 0.5.0-alpha'), 'report: release daemon recognised');

  // Screenshots: real 1920x1080 frames with the expected content.
  const home = readPng(join(shots, 'home.png'));
  check(home.width === 1920 && home.height === 1080, 'home screenshot is 1920x1080');
  check(near(home.pixel(96 - 5 + 1, 830), [0x4f, 0x7c, 0xff], 30), 'home: accent focus ring on the Controller test tile');
  const logoPixel = home.pixel(96 + 19, 34 + 45); // inside the left leg of the "A"
  check(logoPixel[2] > 200 && logoPixel[0] < 160, `home: logo gradient drawn (${logoPixel})`);
  check(near(home.pixel(5, 1075), [6, 9, 18], 40), 'home: dark AKENO background');
  const controller = readPng(join(shots, 'controller.png'));
  check(controller.width === 1920, 'controller screenshot');
  const audio = readPng(join(shots, 'audio.png'));
  check(near(audio.pixel(96 + 40, 576 + 20), [0x4f, 0x7c, 0xff], 40), 'audio: focused Play button');
  const disconnected = readPng(join(shots, 'disconnected.png'));
  check(near(disconnected.pixel(580, 160), [0xf5, 0xa5, 0x24], 40), 'disconnect banner shown');

  // ---- Run 2: audio port refused, daemon absent, token revoked ------------------------------------
  respond401 = true;
  serverRequests.length = 0;
  const r2 = await run(
    {
      ...env,
      AKENO_NATIVE_DAEMON_PORT: '1',
      AKENO_NATIVE_SIM_AUDIO: 'open',
      AKENO_NATIVE_SCRIPT: ['until account 15000', 'press down', 'press right', 'press cross', 'press cross', 'until audio 10000', 'wait 200',
        'expect dialog none', 'expect screen audio', 'press circle', 'press up', 'press left', 'press cross', 'until ipc 10000', 'wait 300',
        'expect screen friends', 'expect text The AKENO daemon is not running', 'press circle', 'press circle', 'expect dialog exit',
        'press left', 'press cross'].join(';'),
    },
    60000,
  );
  check(r2.code === 0, `run 2 exits cleanly (code ${r2.code})\n${r2.output.slice(-2000)}`);
  const report2 = readFileSync(join(nativeDir, 'native-report.txt'), 'utf8');
  check(report2.includes('Outcome:          failed to open the port'), 'run 2: open failure reported');
  check(report2.includes('Open:             0x8026000a'), 'run 2: open error code recorded');
  check(report2.includes('Account:          Pairing no longer accepted'), 'run 2: 401 reported');
  check(report2.includes('HTTP 401 UNAUTHENTICATED'), 'run 2: HTTP status and code');
  check(report2.includes('AKENO daemon:     Not running'), 'run 2: daemon absent');
  check(sha256(credentialPath) === before.credential && statSync(credentialPath).mtimeMs === before.credentialMtime,
    'run 2: the credential survives a 401 untouched');

  // ---- Run 4: the native-IPC daemon (Phase 2.1) ----------------------------------------------------
  const ipc = await startIpcDaemon(akenoDir);
  serverRequests.length = 0;
  respond401 = false;
  const ipcEnv = { ...env, AKENO_NATIVE_DAEMON_PORT: String(ipc.port), AKENO_NATIVE_SYNC_DIR: root };
  const script4 = [
    'until ipc 10000', 'until friends 10000', 'wait 300', 'expect text Live from the AKENO daemon', 'expect text 3 ONLINE',
    'wait 600', 'until pictures 10000', `shot ${shots}/home-ipc.png`,
    // Friends: sorted (online and playing first), details of the focused friend, scrolling
    'pad cross', 'expect screen friends', 'until friends 10000', 'wait 300', 'expect focus row',
    'expect text Bob', 'expect text Playing Astro Bot', 'expect text Ranked tonight', 'expect text 1 friend request waiting',
    'expect text 1–7 of 12', 'until pictures 10000', `shot ${shots}/friends.png`,
    'padhold down 900', 'pad r1', 'expect text 6–12 of 12', 'pad l1', 'pad l1', 'expect text 1–7 of 12',
    // a friend's profile and back to the same row
    'pad cross', 'expect screen profile', 'until profile 10000', 'wait 300', 'expect text Speedrunner.', 'expect text Friday crew', 'expect text 3 of 8 members',
    'expect text Astro Bot', 'expect text @bob', 'expect text PS5 Alpha Tester', 'until pictures 10000',
    `shot ${shots}/profile.png`, 'pad triangle', 'until profile 10000',
    'pad circle', 'expect screen friends', 'expect text Bob',
    // a realtime presence change arrives through the daemon's event ring
    'expect notext Playing Zeta Racer', 'sync carol-online', 'until text 8000 Playing Zeta Racer', 'pad circle', 'expect screen home',
    'expect text 4 ONLINE',
    // your own profile, from Home
    'pad right', 'pad right', 'pad right', 'expect focus profile', 'pad cross', 'expect screen profile', 'until profile 10000', 'wait 300',
    'expect text @akeno_tester', 'expect text you', 'pad circle', 'expect screen home',
    // Activity: this console's game (daemon detection), your games, friends playing now
    'pad right', 'expect focus activity', 'pad cross', 'expect screen activity', 'until activity 10000', 'wait 300',
    'expect text Playing Test Game', 'expect text YOUR RECENT GAMES', 'expect text Gran Turismo 7', 'expect text FRIENDS PLAYING NOW',
    'until pictures 10000', `shot ${shots}/activity.png`, 'pad down', 'pad cross', 'expect screen profile', 'until profile 10000', 'wait 300', 'expect text @carol',
    'pad circle', 'expect screen activity', 'pad circle', 'expect screen home',
    // the daemon restarts with a new key: AKENO Native reads it again and carries on
    // (Dan's new game is only served by the restarted daemon, so seeing it proves the reconnect.)
    'sync rotate-key', 'pad left', 'pad left', 'pad left', 'pad left', 'expect focus friends', 'pad cross',
    'until text 15000 Playing Kart Racer', 'pad circle', 'expect text Live from the AKENO daemon',
    // connection screen names the interface
    'pad down', 'pad right', 'pad right', 'expect focus connection', 'pad cross', 'wait 300', 'expect text Connected to the AKENO daemon',
    `shot ${shots}/connection-ipc.png`,
    'pad circle', 'pad circle', 'expect dialog exit', 'pad left', 'pad cross',
  ].join(';');
  const r4 = await runWithSync({ ...ipcEnv, AKENO_NATIVE_SCRIPT: script4 }, 120000, {
    'carol-online': () => ipc.push('presence.updated', { userId: 'u-carol', presence: presence('online', { titleId: 'PPSA09999', name: 'Zeta Racer' }) }),
    'rotate-key': () => ipc.rotateKey(),
  }, root);
  check(r4.code === 0, `run 4 (native IPC) exits cleanly (code ${r4.code}, signal ${r4.signal})\n${r4.output.slice(-3000)}`);
  const report4 = readFileSync(join(nativeDir, 'native-report.txt'), 'utf8');
  const log4 = readFileSync(join(nativeDir, 'native.log'), 'utf8');
  check(report4.includes('State:            Connected to the AKENO daemon: protocol 1, daemon 0.5.0-alpha (fake-ps5-native-ipc)'), 'report 4: IPC state');
  check(/Friends:\s+12 \(4 online, 3 playing\), 1 incoming and 0 outgoing requests/.test(report4), `report 4: friend counts\n${report4}`);
  check(/Requests:\s+\d+ \(GET only\), \d+ errors, \d+ event polls, [1-9]\d* events, 2 connects/.test(report4), `report 4: request counters\n${report4}`);
  check(log4.includes('ipc.state: Connected to the AKENO daemon'), 'log 4: connected');
  check(log4.includes('social.friends: 12 friends, 3 online, 1 incoming requests'), 'log 4: counts, no names');
  for (const text of [report4, log4]) {
    for (const secret of [ipc.firstKey, ipc.key]) check(!text.includes(secret), 'native key never in report or log');
    for (const name of ['Ada Lovelace', '@bob', 'Speedrunner', 'Zeta Racer', 'Kart Racer', 'Friday crew']) check(!text.includes(name), `no other person's data in report or log (${name})`);
  }
  const methods = new Set(ipc.requests.map((r) => r.method));
  check(methods.size === 1 && methods.has('GET'), `Phase 2.1 sends GET only: ${[...methods]}`);
  check(ipc.requests.every((r) => !r.origin), 'no Origin header on native requests');
  check(ipc.requests.filter((r) => r.path !== '/api/instance').every((r) => r.key), 'every interface request carries the native key');
  check(ipc.requests.filter((r) => r.path === '/api/instance').every((r) => !r.key), 'the liveness probe never carries the key');
  const proxied = ipc.requests.filter((r) => r.path.startsWith('/api/v1/')).map((r) => r.path);
  check(proxied.includes('/api/v1/friends') && proxied.includes('/api/v1/friends/requests') && proxied.includes('/api/v1/users/u-bob') &&
    proxied.includes('/api/v1/users/u-bob/activity') && proxied.includes('/api/v1/users/akeno_tester') &&
    proxied.includes('/api/v1/users/akeno_tester/activity') && proxied.includes('/api/v1/users/u-carol'), `proxied paths: ${proxied.join(' ')}`);
  check(ipc.requests.some((r) => r.path.startsWith('/api/events?after=')), 'the event ring is polled');
  check(ipc.requests.filter((r) => r.path === '/api/native/hello').length === 2, 'hello once, and once more after the key changed');
  check(ipc.rejected >= 1 && ipc.rejected <= 3, `old key refused briefly, then the new one used (${ipc.rejected} refusals)`);
  check(serverRequests.every((r) => r === 'GET /api/v1/devices/current token'), `the server saw only the connection check: ${serverRequests.join(' | ')}`);
  check(readdirSync(akenoDir).sort().join(',') === [...before.files.split(','), 'native.key'].sort().join(','), 'AKENO Native added nothing to the daemon folder');
  check(sha256(credentialPath) === before.credential && sha256(settingsPath) === before.settings, 'run 4: daemon files unchanged');
  const friendsShot = readPng(join(shots, 'friends.png'));
  check(near(friendsShot.pixel(96 - 5 + 1, 290 + 40), [0x4f, 0x7c, 0xff], 40), 'friends: focus ring on the first row');
  const av = friendsShot.pixel(96 + 50 - 24, 290 + 42);
  check(Math.max(...av.slice(0, 3)) - Math.min(...av.slice(0, 3)) > 60, `friends: a coloured avatar is drawn (${av})`);

  // Pictures: real profile pictures, banner, game artwork and the console's icon, drawn from the
  // daemon's answers; a corrupt and a missing picture leave the initials; nothing about them logged.
  check(near(av, [224, 16, 16], 40), `friends: Bob's profile picture (red) on his row (${av})`);
  check(near(friendsShot.pixel(96 + 1000 - 76 + 30, 290 + 42), [250, 200, 20], 40), 'friends: Astro Bot artwork on the row of a friend playing it');
  const profileShot = readPng(join(shots, 'profile.png'));
  check(near(profileShot.pixel(276, 450), [224, 16, 16], 40), `profile: Bob's picture in the large circle (${profileShot.pixel(276, 450)})`);
  check(near(profileShot.pixel(1000, 250), [16, 192, 64], 40), `profile: Bob's banner (${profileShot.pixel(1000, 250)})`);
  check(!near(profileShot.pixel(276 - 118, 528 - 118), [224, 16, 16], 60), 'profile: the picture is a circle (corner not filled)');
  const homeIpc = readPng(join(shots, 'home-ipc.png'));
  check(near(homeIpc.pixel(96 + 74, 248), [20, 200, 220], 40), `home: your own profile picture (${homeIpc.pixel(96 + 74, 248)})`);
  const activityShot = readPng(join(shots, 'activity.png'));
  check(near(activityShot.pixel(96 + 36 + 70, 290 + 66 + 70), [240, 120, 0], 40), `activity: this console's icon of the running game (${activityShot.pixel(96 + 36 + 70, 290 + 66 + 70)})`);
  check(near(activityShot.pixel(976 + 36 + 28, 452), [250, 200, 20], 40), `activity: the cover of a recent game (${activityShot.pixel(976 + 36 + 28, 452)})`);
  check(ipc.media.length > 0 && ipc.media.every((m) => m.variant === 'fallback'), 'pictures: only the JPEG variant is asked for');
  check(ipc.media.every((m) => /^[0-9a-f]{32}$/.test(m.id) && Object.values(MEDIA).includes(m.id)), 'pictures: only media IDs from the server answers');
  const fetches = (id) => ipc.media.filter((m) => m.id === id).length;
  check(fetches(MEDIA.carolAvatar) >= 1 && fetches(MEDIA.carolAvatar) <= 3, `pictures: a corrupt picture is not asked for again and again (${fetches(MEDIA.carolAvatar)})`);
  check(fetches(MEDIA.danAvatar) >= 1 && fetches(MEDIA.danAvatar) <= 3, `pictures: a missing picture is not asked for again and again (${fetches(MEDIA.danAvatar)})`);
  check(ipc.localArt.every((t) => /^[A-Z]{4}\d{5}$/.test(t)), 'pictures: local art only for title IDs');
  check(log4.includes('images.failed: picture: decode:'), 'log 4: the corrupt picture is reported');
  check(/Pictures:\s+\d+ asked for, [1-9]\d* shown, [1-9]\d* missing or unreadable/.test(report4), `report 4: picture counters\n${report4}`);
  for (const text of [report4, log4]) {
    for (const id of Object.values(MEDIA)) check(!text.includes(id), 'no media ID in report or log');
    check(!text.includes('appmeta'), 'no console icon path in report or log');
  }

  // ---- Run 5: an unsafe key file is not used --------------------------------------------------------
  ipc.requests.length = 0;
  chmodSync(join(akenoDir, 'native.key'), 0o644);
  const r5 = await run({ ...ipcEnv, AKENO_NATIVE_SCRIPT: ['until ipc 10000', 'pad cross', 'wait 300', 'expect text native key file is not private',
    'expect text not a private file', 'pad circle', 'pad circle', 'pad left', 'pad cross'].join(';') }, 60000);
  check(r5.code === 0, `run 5 (unsafe key) exits cleanly (code ${r5.code})\n${r5.output.slice(-2000)}`);
  check(ipc.requests.every((r) => !r.key), 'run 5: a key from an unsafe file is never sent');
  ipc.close();

  // ---- Run 3: a broken script fails the run ------------------------------------------------------
  const r3 = await run({ ...env, AKENO_NATIVE_SCRIPT: 'expect screen audio' }, 30000);
  check(r3.code === 3, `a failed expectation exits with 3 (got ${r3.code})`);

  if (process.env.AKN_IT_OUT) {
    mkdirSync(process.env.AKN_IT_OUT, { recursive: true });
    for (const f of readdirSync(shots)) copyFileSync(join(shots, f), join(process.env.AKN_IT_OUT, f));
    writeFileSync(join(process.env.AKN_IT_OUT, 'native-report-run1.txt'), report);
  }
  server.close();
  daemon.close();
  if (failures === 0) rmSync(root, { recursive: true, force: true });
  else console.error(`Artifacts kept in ${root}`);
  console.log(`${checks} checks, ${failures} failures`);
  process.exit(failures === 0 ? 0 : 1);
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
