import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  base: '/',
  build: {
    outDir: 'usrPages',
    emptyOutDir: true,
    sourcemap: false,
  },
});
