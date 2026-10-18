// A mock of the GitHub endpoints the updater uses, on loopback, for tests/updater/run.mjs:
//
//   GET /repos/<owner>/<repo>/releases?per_page=30          the releases list (JSON)
//   GET /<owner>/<repo>/releases/download/<tag>/<name>      302 to the asset host, like github.com
//   GET /assets/<tag>/<name>                                the bytes ("localhost" host name)
//
// Faults are set per test through server.fault: 'rate-limit', 'truncate' (close half-way),
// 'stall' (send half, then nothing), 'redirect-foreign' (302 to an address not on the allow-list),
// 'http-500', 'chunked' (serve assets with chunked transfer encoding), 'corrupt' (flip a byte).
import { createServer } from 'node:http';

export const OWNER_REPO = 'YTDxDAkeno/AKENO-NETWORK';

export function startMock() {
  const state = { releases: [], assets: new Map(), fault: null, faultAsset: null, requests: [] };
  const server = createServer((req, res) => {
    const url = new URL(req.url, 'http://x');
    state.requests.push(`${req.method} ${req.headers.host} ${url.pathname}`);
    const port = server.address().port;
    if (req.method !== 'GET') {
      res.writeHead(405).end();
      return;
    }
    if (url.pathname === `/repos/${OWNER_REPO}/releases`) {
      if (state.fault === 'rate-limit') {
        res.writeHead(403, { 'Content-Type': 'application/json', 'X-RateLimit-Remaining': '0' });
        res.end('{"message":"API rate limit exceeded"}');
        return;
      }
      if (state.fault === 'http-500') {
        res.writeHead(500).end('oops');
        return;
      }
      const body = JSON.stringify(state.releases);
      res.writeHead(200, { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body) });
      res.end(body);
      return;
    }
    const dl = url.pathname.match(new RegExp(`^/${OWNER_REPO}/releases/download/([^/]+)/([^/]+)$`));
    if (dl) {
      const host = state.fault === 'redirect-foreign' && (!state.faultAsset || state.faultAsset === dl[2]) ? '127.0.0.2' : 'localhost';
      res.writeHead(302, { Location: `http://${host}:${port}/assets/${dl[1]}/${dl[2]}` });
      res.end();
      return;
    }
    const as = url.pathname.match(/^\/assets\/([^/]+)\/([^/]+)$/);
    if (as && state.assets.has(`${as[1]}/${as[2]}`)) {
      let data = state.assets.get(`${as[1]}/${as[2]}`);
      const hit = !state.faultAsset || state.faultAsset === as[2];
      if (state.fault === 'corrupt' && hit) {
        data = Buffer.from(data);
        data[data.length >> 1] ^= 0x55;
      }
      if (state.fault === 'chunked' && hit) {
        res.writeHead(200, { 'Content-Type': 'application/octet-stream' });
        for (let i = 0; i < data.length; i += 7000) res.write(data.subarray(i, i + 7000));
        res.end();
        return;
      }
      res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Length': data.length });
      if ((state.fault === 'truncate' || state.fault === 'stall') && hit) {
        res.write(data.subarray(0, data.length >> 1));
        if (state.fault === 'truncate') setTimeout(() => res.destroy(), 50);
        return; // 'stall': keep the connection open without sending more
      }
      res.end(data);
      return;
    }
    res.writeHead(404).end('not found');
  });
  return new Promise((resolve) => {
    server.listen(0, '127.0.0.1', () => {
      const port = server.address().port;
      const base = `http://127.0.0.1:${port}`;
      resolve({
        state,
        port,
        api: `${base}/repos/${OWNER_REPO}/releases?per_page=30`,
        prefix: `${base}/${OWNER_REPO}/releases/download/`,
        // Publishes a release built by tools/make-update.mjs (buildRelease) with these extra fields.
        publish(rel, { prerelease = false, draft = false, body = 'Changes', assetsOverride, urlRepo = OWNER_REPO } = {}) {
          const files = { 'akeno-native-update.json': rel.manifestText, 'akeno-native-update.json.sig': rel.signature, [rel.packageName]: rel.zip };
          for (const [name, data] of Object.entries(files)) state.assets.set(`${rel.tag}/${name}`, data);
          const assets = Object.entries(files).map(([name, data], i) => ({
            id: i + 1,
            name,
            state: 'uploaded',
            size: data.length,
            browser_download_url: `${base}/${urlRepo}/releases/download/${rel.tag}/${name}`,
          }));
          state.releases.unshift({
            tag_name: rel.tag,
            name: `AKENO NETWORK Native ${rel.manifest.version}`,
            draft,
            prerelease,
            published_at: '2026-10-18T10:00:00Z',
            html_url: `https://github.com/${OWNER_REPO}/releases/tag/${rel.tag}`,
            body,
            assets: assetsOverride ? assetsOverride(assets) : assets,
          });
        },
        close: () => new Promise((r) => {
          server.closeAllConnections?.();
          server.close(() => r());
        }),
      });
    });
  });
}
