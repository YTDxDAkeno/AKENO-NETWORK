// Bundles the server into dist/ with esbuild. Third-party dependencies stay external and are
// installed from package.json at deploy time; the workspace package @akeno/shared is inlined.
import { build } from 'esbuild';
import { cp, readFile, rm } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('..', import.meta.url));
const pkg = JSON.parse(await readFile(new URL('../package.json', import.meta.url), 'utf8'));
const external = Object.keys(pkg.dependencies ?? {});

await rm(new URL('../dist', import.meta.url), { recursive: true, force: true });

await build({
  absWorkingDir: root,
  entryPoints: { main: 'src/main.ts', cli: 'src/cli.ts' },
  outdir: 'dist',
  bundle: true,
  platform: 'node',
  format: 'esm',
  target: 'node22',
  sourcemap: true,
  external: [...external, 'pino-pretty'],
  // Some CommonJS dependencies expect require() to exist in ESM output.
  banner: {
    js: "import { createRequire as __akenoCreateRequire } from 'node:module'; const require = __akenoCreateRequire(import.meta.url);",
  },
  logLevel: 'info',
});

await cp(new URL('../src/database/migrations', import.meta.url), new URL('../dist/migrations', import.meta.url), {
  recursive: true,
});

console.log('Server build complete: dist/main.js, dist/cli.js, dist/migrations/');
