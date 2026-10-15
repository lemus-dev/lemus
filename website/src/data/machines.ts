// One entry per released plugin. The home page's plugin table, the nav, the download links and
// /latest.json (the plugins' update check) are built from this list.

export interface Release {
  version: string;
  stage?: 'alpha' | 'beta'; // a pre-release: shown next to the version, with the unsigned-installer warning
  mac: string; // installer paths under /download
  windows: string;
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
    release: { version: '0.1.1', stage: 'alpha', mac: 'ro-mono-303-0.1.1-mac.pkg', windows: 'ro-mono-303-0.1.1-windows.zip' },
  },
];

export const bySlug = (slug: string) => machines.find((m) => m.slug === slug)!;

export const downloadLinks = (m: Machine) =>
  m.release
    ? { mac: `/download/${m.release.mac}`, windows: `/download/${m.release.windows}` }
    : { mac: '/download', windows: '/download' };

export const platforms =
  'macOS 11+ (Universal): AU, VST3, standalone · Windows 10+: VST3, standalone';
