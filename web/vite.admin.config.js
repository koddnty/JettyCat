import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import { resolve } from 'path';

export default defineConfig({
  root: resolve(__dirname, 'admin'),
  plugins: [react()],
  base: '/admin/',
  publicDir: false,
  build: {
    outDir: resolve(__dirname, 'adminPages'),
    emptyOutDir: true,
    sourcemap: false,
    rollupOptions: {
      input: {
        'pages/index': resolve(__dirname, 'admin/pages/index.html'),
      },
    },
  },
});
