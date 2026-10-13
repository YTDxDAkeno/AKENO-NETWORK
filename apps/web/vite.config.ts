import tailwindcss from '@tailwindcss/vite';
import react from '@vitejs/plugin-react';
import { defineConfig } from 'vite';

const API_TARGET = process.env.AKENO_API_URL ?? 'http://127.0.0.1:3000';

export default defineConfig({
  plugins: [react(), tailwindcss()],
  server: {
    port: 5173,
    strictPort: true,
    // The dev server proxies API and WebSocket traffic so the browser sees one origin,
    // exactly like production behind Caddy.
    proxy: {
      '/api': { target: API_TARGET },
      '/health': { target: API_TARGET },
      '/ws': { target: API_TARGET.replace(/^http/, 'ws'), ws: true },
    },
  },
  build: {
    target: 'es2022',
    sourcemap: false,
    chunkSizeWarningLimit: 800,
  },
});
