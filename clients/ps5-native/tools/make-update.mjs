#!/usr/bin/env node
// Builds the three release assets the updater reads (docs/PS5_NATIVE_UPDATER.md, "Release format"):
//
//   akeno-native-<version>.zip          the files, stored or deflated, no directories entries needed
//   akeno-native-update.json            the manifest: version, tag, channel, package size and SHA-256,
//                                       every file's size and SHA-256, compatibility
//   akeno-native-update.json.sig        ECDSA P-256 / SHA-256 signature of the manifest bytes (DER)
//
//   node tools/make-update.mjs --version 1.2.0 --channel stable --out DIR \
//        --file eboot.elf=build-ps5/akeno-native.elf --file homebrew.js=package/homebrew.js \
//        --file sce_sys/icon0.png=icon0.png [--firmware-min 12.00] [--firmware-max 12.99] \
//        [--daemon-protocol-min 1] [--daemon-protocol-max 1] [--min-installed 1.1.0] [--notes-file NOTES.md]
//
// The private key comes from the environment (AKN_UPDATE_SIGNING_KEY, PEM) or --key FILE; it is
// never written anywhere. Nothing is uploaded: publishing a release is a separate, manual step.
import { createHash, createPrivateKey, sign } from 'node:crypto';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';
import { deflateRawSync } from 'node:zlib';

export const MANIFEST = 'akeno-native-update.json';
export const SIGNATURE = 'akeno-native-update.json.sig';
export const TAG_PREFIX = 'native-';

const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();

export function crc32(buf) {
  let c = 0xffffffff;
  for (const b of buf) c = CRC_TABLE[(c ^ b) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

export const sha256 = (buf) => createHash('sha256').update(buf).digest('hex');

// entries: [{ name, data: Buffer, method?: 0|8, externalAttr?, crc?, nameInCentral? }]
// The optional fields exist so the tests can build damaged or hostile archives.
export function buildZip(entries) {
  const locals = [];
  const centrals = [];
  let offset = 0;
  for (const e of entries) {
    const method = e.method ?? 8;
    const comp = method === 8 ? deflateRawSync(e.data, { level: 9 }) : e.data;
    const name = Buffer.from(e.name);
    const crc = e.crc ?? crc32(e.data);
    const local = Buffer.alloc(30);
    local.writeUInt32LE(0x04034b50, 0);
    local.writeUInt16LE(20, 4);
    local.writeUInt16LE(e.flags ?? 0, 6);
    local.writeUInt16LE(method, 8);
    local.writeUInt32LE(0x00210000, 10); // 1980-01-01, fixed for reproducible packages
    local.writeUInt32LE(crc, 14);
    local.writeUInt32LE(comp.length, 18);
    local.writeUInt32LE(e.data.length, 22);
    local.writeUInt16LE(name.length, 26);
    local.writeUInt16LE(0, 28);
    const cname = Buffer.from(e.nameInCentral ?? e.name);
    const central = Buffer.alloc(46);
    central.writeUInt32LE(0x02014b50, 0);
    central.writeUInt16LE(0x0314, 4); // made by unix, 2.0
    central.writeUInt16LE(20, 6);
    central.writeUInt16LE(e.flags ?? 0, 8);
    central.writeUInt16LE(method, 10);
    central.writeUInt32LE(0x00210000, 12);
    central.writeUInt32LE(crc, 16);
    central.writeUInt32LE(comp.length, 20);
    central.writeUInt32LE(e.data.length, 24);
    central.writeUInt16LE(cname.length, 28);
    central.writeUInt32LE(e.externalAttr ?? (0o100644 << 16) >>> 0, 38);
    central.writeUInt32LE(offset, 42);
    locals.push(local, name, comp);
    centrals.push(central, cname);
    offset += local.length + name.length + comp.length;
  }
  const cd = Buffer.concat(centrals);
  const eocd = Buffer.alloc(22);
  eocd.writeUInt32LE(0x06054b50, 0);
  eocd.writeUInt16LE(entries.length, 8);
  eocd.writeUInt16LE(entries.length, 10);
  eocd.writeUInt32LE(cd.length, 12);
  eocd.writeUInt32LE(offset, 16);
  return Buffer.concat([...locals, cd, eocd]);
}

// files: [{ path, data }]; returns { zip, manifestText, signature, packageName, tag }
export function buildRelease({ version, channel, files, key, requires = {}, notes = '', manifestOverride, zipOverride }) {
  const tag = TAG_PREFIX + version;
  const packageName = `akeno-native-${version}.zip`;
  const zip = zipOverride ?? buildZip(files.map((f) => ({ name: f.path, data: f.data })));
  const manifest = {
    format: 1,
    product: 'akeno-network-native',
    version,
    tag,
    channel,
    package: { name: packageName, size: zip.length, sha256: sha256(zip) },
    files: files.map((f) => ({ path: f.path, size: f.data.length, sha256: sha256(f.data) })),
    requires,
    notes,
    ...(manifestOverride ?? {}),
  };
  const manifestText = Buffer.from(JSON.stringify(manifest, null, 2) + '\n');
  const signature = sign('sha256', manifestText, createPrivateKey(key)); // DER (ASN.1) ECDSA
  return { zip, manifestText, signature, packageName, tag, manifest };
}

function parseArgs(argv) {
  const o = { file: [] };
  for (let i = 0; i < argv.length; i += 2) {
    const k = argv[i].replace(/^--/, '');
    const v = argv[i + 1];
    if (v === undefined) throw new Error(`missing value for ${argv[i]}`);
    if (k === 'file') o.file.push(v);
    else o[k] = v;
  }
  return o;
}

async function main() {
  const o = parseArgs(process.argv.slice(2));
  if (!o.version || !o.out || !o.file.length) throw new Error('usage: see the header of tools/make-update.mjs');
  const key = process.env.AKN_UPDATE_SIGNING_KEY || (o.key && readFileSync(o.key, 'utf8'));
  if (!key) throw new Error('no signing key: set AKN_UPDATE_SIGNING_KEY or pass --key');
  const channel = o.channel ?? (o.version.includes('-') ? 'beta' : 'stable');
  const files = o.file.map((spec) => {
    const eq = spec.indexOf('=');
    if (eq <= 0) throw new Error(`--file wants path=source, got ${spec}`);
    return { path: spec.slice(0, eq), data: readFileSync(spec.slice(eq + 1)) };
  });
  const requires = {};
  if (o['firmware-min']) requires.firmware_min = o['firmware-min'];
  if (o['firmware-max']) requires.firmware_max = o['firmware-max'];
  if (o['daemon-protocol-min']) requires.daemon_protocol_min = Number(o['daemon-protocol-min']);
  if (o['daemon-protocol-max']) requires.daemon_protocol_max = Number(o['daemon-protocol-max']);
  if (o['min-installed']) requires.min_installed_version = o['min-installed'];
  const notes = o['notes-file'] ? readFileSync(o['notes-file'], 'utf8').slice(0, 8000) : '';
  const r = buildRelease({ version: o.version, channel, files, key, requires, notes });
  mkdirSync(o.out, { recursive: true });
  writeFileSync(join(o.out, r.packageName), r.zip);
  writeFileSync(join(o.out, MANIFEST), r.manifestText);
  writeFileSync(join(o.out, SIGNATURE), r.signature);
  console.log(`tag ${r.tag} (${channel}): ${r.packageName} ${r.zip.length} bytes, sha256 ${r.manifest.package.sha256}`);
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  main().catch((e) => {
    console.error(e.message);
    process.exit(1);
  });
}
