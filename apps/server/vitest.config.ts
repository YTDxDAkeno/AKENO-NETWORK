import { defineConfig } from 'vitest/config';

export default defineConfig({
  test: {
    include: ['test/**/*.test.ts'],
    globalSetup: ['./test/global-setup.ts'],
    testTimeout: 30_000,
    hookTimeout: 60_000,
    pool: 'forks',
    // Each test file creates its own database from a migrated template, so files may run in parallel.
    fileParallelism: true,
    maxWorkers: 4,
  },
});
