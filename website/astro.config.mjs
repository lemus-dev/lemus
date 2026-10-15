import { defineConfig } from 'astro/config';

export default defineConfig({
  site: 'https://lemusdev.org',
  // /support, not /support/: the plugins link to these paths.
  build: { format: 'file' },
  trailingSlash: 'never',
});
