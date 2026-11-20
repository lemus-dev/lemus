// Lemus Plugin Manager (manager/ in the repo): the app that installs and updates the plugins. Its
// release, for /latest.json (the manager reads that file, and its own entry tells it when it is
// out of date) and the download page. null until it is released.
import type { Change, Release } from './machines';

export const manager: { slug: string; page: string; release: Release | null; changes: Change[] } = {
  slug: 'lemus-plugin-manager',
  page: '/plugin-manager',
  release: {
    version: '0.1.0',
    tag: 'lemus-plugin-manager-v0.1.0',
    stage: 'beta',
    mac: 'lemus-plugin-manager-0.1.0-mac.pkg',
    windows: 'lemus-plugin-manager-0.1.0-windows.zip',
  },
  changes: [
    {
      version: '0.1.0',
      notes: ['First release: lists the released plugins and installs, updates and removes them; checks every download\'s SHA-256; updates itself.'],
    },
  ],
};
