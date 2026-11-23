// Lemus Plugin Manager (manager/ in the repo): the app that installs and updates the plugins. Its
// release, for /latest.json (the manager reads that file, and its own entry tells it when it is
// out of date) and the download page. null until it is released.
import type { Change, Release } from './machines';

export const manager: { slug: string; page: string; release: Release | null; changes: Change[] } = {
  slug: 'lemus-plugin-manager',
  page: '/plugin-manager',
  release: {
    version: '0.2.0',
    tag: 'lemus-plugin-manager-v0.2.0',
    stage: 'beta',
    mac: 'lemus-plugin-manager-0.2.0-mac.pkg',
    windows: 'lemus-plugin-manager-0.2.0-windows.zip',
  },
  changes: [
    {
      version: '0.2.0',
      notes: [
        'Shows whether each plugin has its ROMs and artwork: ROM and ART columns, and in its details each ROM found or not, with the size, CRC and SHA1 of a missing one.',
        'Drop ROMs or artwork on the window (files, folders or zips): matched by SHA1 or file name and copied into the plugin\'s ROM folder.',
        'A Manage menu for each plugin: its page, Show in Finder or Explorer, Show ROM folder, Copy ROM details, Remove.',
        'Plugins can open it from their what\'s-new pop-up, from each plugin\'s next release.',
      ],
    },
    {
      version: '0.1.0',
      notes: ['First release: lists the released plugins and installs, updates and removes them; checks every download\'s SHA-256; updates itself.'],
    },
  ],
};
