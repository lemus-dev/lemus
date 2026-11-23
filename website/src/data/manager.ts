// Lemus Plugin Manager (manager/ in the repo): the app that installs and updates the plugins. Its
// release, for /latest.json (the manager reads that file, and its own entry tells it when it is
// out of date) and the download page. null until it is released.
import type { Change, Release } from './machines';

export const manager: { slug: string; page: string; release: Release | null; changes: Change[] } = {
  slug: 'lemus-plugin-manager',
  page: '/plugin-manager',
  release: {
    version: '0.2.2',
    tag: 'lemus-plugin-manager-v0.2.2',
    stage: 'beta',
    mac: 'lemus-plugin-manager-0.2.2-mac.pkg',
    windows: 'lemus-plugin-manager-0.2.2-windows.zip',
  },
  changes: [
    {
      version: '0.2.2',
      notes: [
        'A plugin\'s ROM can have alternatives: al-fx-quadgt takes firmware v1.00 (qvgtv100.bin) as well as v1.03. A dropped one goes into the ROM folder under its own name.',
        'The older hd44780_a00.bin drawn from the datasheet (CRC 01d108e2), dropped on the window, goes in corrected, so al-fx-quadgt finds it.',
      ],
    },
    {
      version: '0.2.1',
      notes: ['The list fits the longest names (the Oberheim Xpander was cut short); the artwork hint names a real file.'],
    },
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
