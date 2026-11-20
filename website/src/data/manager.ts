// Lemus Plugin Manager (manager/ in the repo): the app that installs and updates the plugins. Its
// release, for /latest.json (the manager reads that file, and its own entry tells it when it is
// out of date) and the download page. null until it is released.
import type { Change, Release } from './machines';

export const manager: { slug: string; page: string; release: Release | null; changes: Change[] } = {
  slug: 'lemus-plugin-manager',
  page: '/plugin-manager',
  release: null,
  changes: [],
};
