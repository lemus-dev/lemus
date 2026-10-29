// One entry per released plugin. The home page's plugin table, the nav, the download links and
// /latest.json (the plugins' update check) are built from this list.

export interface Release {
  version: string;
  stage?: 'alpha' | 'beta'; // a pre-release: shown next to the version, with the unsigned-installer warning
  mac: string; // installer file names, attached to the GitHub release (see assetOf)
  windows: string;
}

// What a version changed, for its GitHub release and the plugin's what's-new window (via
// /latest.json). Short lines, newest version first; the next version's can go in before it ships.
export interface Change {
  version: string;
  notes: string[];
}

export interface Machine {
  slug: string; // the plugin's name, and its page: /<slug>
  emulates: string;
  year: number;
  type: string;
  status: 'working' | 'preliminary';
  withoutRom: 'voice' | 'none';
  keywords: string; // extra words the plugin search matches
  nav: boolean; // listed in the header
  release: Release | null; // null: not downloadable yet, links go to /download
  changes: Change[];
}

export const machines: Machine[] = [
  {
    slug: 'ro-mono-303',
    emulates: 'Roland TB-303 Bass Line',
    year: 1981,
    type: 'mono synth',
    status: 'working',
    withoutRom: 'voice',
    keywords: '303 bass acid sequencer roland',
    nav: true,
    release: { version: '0.1.3', stage: 'beta', mac: 'ro-mono-303-0.1.3-mac.pkg', windows: 'ro-mono-303-0.1.3-windows.zip' },
    changes: [
      {
        version: '0.1.4',
        notes: [
          "From this version on, a new release shows what's new in a window: Download, Remind me later (a week) or Skip this version.",
          'About shows the ROM folder in use, with a link that opens it.',
          'After Locate ROM, the bar says which folder the ROM went into.',
          'Changing the ROM folder removes the old folder\'s tb303.svg if it is still the untouched blank.',
          'The status bar names a panel key held down by a right-click. FUNCTION does nothing while TAP is held, as on the TB-303.',
        ],
      },
      {
        version: '0.1.3',
        notes: ['The installers include the AGPLv3 (LICENSE.txt) and NOTICES.txt; the About box names the licence and links the source.'],
      },
      {
        version: '0.1.2',
        notes: [
          'No CLEAR MEMORY in the bar.',
          "A right-click holds a panel key down until it's clicked again.",
          'The ROM prompt comes back if the ROM goes missing.',
        ],
      },
    ],
  },
];

// True if version a is newer than b, number by number (0.10.0 is newer than 0.9.1).
export const isNewer = (a: string, b: string) => {
  const x = a.split('.').map(Number), y = b.split('.').map(Number);
  for (let i = 0; i < Math.max(x.length, y.length); i++)
    if ((x[i] || 0) !== (y[i] || 0)) return (x[i] || 0) > (y[i] || 0);
  return false;
};

export const bySlug = (slug: string) => machines.find((m) => m.slug === slug)!;

// The source of a release, next to its downloads: the binaries are AGPLv3, and the licence
// accepts the source offered from the same place. Each release is tagged v<version> there.
export const sourceOf = (r: Release) => `https://github.com/lemus-dev/lemus/tree/v${r.version}`;

// The installers are attached to the release on GitHub (tag v<version>), which counts their
// downloads. public/_redirects sends the old /download/<file> links there.
export const assetOf = (r: Release, file: string) =>
  `https://github.com/lemus-dev/lemus/releases/download/v${r.version}/${file}`;

export const downloadLinks = (m: Machine) =>
  m.release
    ? { mac: assetOf(m.release, m.release.mac), windows: assetOf(m.release, m.release.windows) }
    : { mac: '/download', windows: '/download' };

// Each system the plugins run on, and the formats for it.
export const platforms: [string, string][] = [
  ['macOS 11+', 'Universal: AU, VST3, standalone'],
  ['Windows 10+', 'VST3, standalone'],
];
