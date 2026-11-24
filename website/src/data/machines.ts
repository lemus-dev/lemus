// One entry per released plugin. The home page's plugin table, the nav, the download links and
// /latest.json (the plugins' update check) are built from this list.

export interface Release {
  version: string; // the plugin's own (its lemus_plugin() VERSION in the repo)
  tag: string; // the release's tag in lemus-dev/lemus: <slug>-v<version>; v<version> up to 0.2.0, when every plugin shared one version
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
  withoutRom: 'voice' | 'none' | 'not-needed'; // not-needed: a circuit with no ROM at all
  standalone?: boolean; // ships a standalone app (default true)
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
    release: { version: '0.2.6', tag: 'ro-mono-303-v0.2.6', stage: 'beta', mac: 'ro-mono-303-0.2.6-mac.pkg', windows: 'ro-mono-303-0.2.6-windows.zip' },
    changes: [
      {
        version: '0.2.6',
        notes: [
          "With Sync to Host on, the pattern now lands on the DAW's grid in playback and bounces: the plugin reports the firmware's 1.6 ms lag behind the clock as its latency, so the DAW takes it off. MIDI notes are held back to match.",
        ],
      },
      {
        version: '0.2.5',
        notes: [
          'A reopened song keeps its selected pattern. Sessions now save and load the whole unit, CPU included, instead of starting it as at power-on; only the POWER switch does that. Songs saved with earlier versions reopen on their pattern too.',
        ],
      },
      {
        version: '0.2.4',
        notes: [
          'Computer keys by place now work in the plugin: 0.2.3 had left them out of the build, so German (QWERTZ) and French (AZERTY) keyboards still played the keys by their labels.',
          'A few rare circuit solves that could stop short (the square shaper, two envelope diodes, the VCA near silence) now always finish. No audible change.',
        ],
      },
      {
        version: '0.2.3',
        notes: [
          'Computer keys go by place, as on a US keyboard: on a German (QWERTZ) or French (AZERTY) keyboard the note, function and mode keys sit where the key list shows them.',
          'Command (Ctrl on Windows) with a key now goes on to the DAW instead of pressing a panel key.',
        ],
      },
      {
        version: '0.2.2',
        notes: ['Windows: built with JUCE 8.0.15, which fixes window drawing that could stop the host taking mouse clicks (seen with ro-drums-909 in Ableton Live).'],
      },
      {
        version: '0.2.1',
        notes: [
          "The ROM folder is per plugin: choosing one here no longer changes another plugin's.",
          'Until a plugin has found its ROMs or artwork, it uses the last folder chosen in any plugin.',
          'Artwork can be named after the plugin, anywhere in the ROM folder: ro-mono-303-maker.svg or .png. artwork/tb303/ still works.',
        ],
      },
      {
        version: '0.2.0',
        notes: [
          'State in the top bar has Reset…: the instance as a new one, every control and SERVICE trim at its default.',
          'FACTORY TRIM is gone from SERVICE: State, Reset… puts the trims back.',
          'A key held down by a right-click stays down when its computer key is let go.',
          'Every button in the top bar has a tooltip.',
          'The ROM prompt says what the CPU ROM holds, and what the 303 keeps and loses without it.',
          'BETA next to the name in the top bar.',
        ],
      },
      {
        version: '0.1.5',
        notes: [
          'Artwork: an SVG or PNG of your own for each place on the panel (maker, name, model, tagline), in artwork/tb303/ in the ROM folder, drawn as it looks. A tb303.svg still works.',
          'ROMs in the top bar shows the ROM folder, the CPU ROM and the artwork with their full paths, and changes or rescans the folder. In voice mode it is Add CPU ROM….',
          'State in the top bar saves the instance to a file and loads one, the standalone app\'s saved states too.',
          'MIDI: a note overlapping one of the same pitch slides into it instead of cutting it, and with the sustain pedal down every note slides.',
          'The Mac installer runs on macOS 11 and later again: 0.1.0 to 0.1.4 needed the newest macOS.',
          'About links to the forum, lemus.discourse.group.',
          'No blank tb303.svg is written into the ROM folder any more, and an untouched one is taken out.',
        ],
      },
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
  {
    slug: 'ro-drums-909',
    emulates: 'Roland TR-909 Rhythm Composer',
    year: 1984,
    type: 'drum machine',
    status: 'working',
    withoutRom: 'voice',
    keywords: '909 drums drum machine rhythm composer roland',
    nav: true,
    release: { version: '0.2.6', tag: 'ro-drums-909-v0.2.6', stage: 'beta', mac: 'ro-drums-909-0.2.6-mac.pkg', windows: 'ro-drums-909-0.2.6-windows.zip' },
    changes: [
      {
        version: '0.2.7',
        notes: [
          'BD pitch, in SERVICE under PARTS: where the kick settles, which differs from 909 to 909 more than anything else in it. It goes from the measured unit\'s 49.35 Hz, the default and the sound so far, to 54.19 Hz with the printed C14 and R27; two other units settle at 52.3-52.7 Hz.',
          "In SERVICE, a part under the mouse is ringed on the board and the scope's caption says what it does. A dot on SERVICE shows TM or a part moved off the unit's own.",
        ],
      },
      {
        version: '0.2.6',
        notes: [
          "With SYNC on HOST, the patterns now land on the DAW's grid in playback and bounces: the plugin reports the firmware's 3.2 ms lag behind the MIDI clock as its latency, so the DAW takes it off. MIDI notes are held back 1.4 ms to match (the firmware takes the rest).",
        ],
      },
      {
        version: '0.2.5',
        notes: [
          "With SYNC on HOST and the DAW's loop on, the 909 now plays every pass of the loop. Before, it stopped at the loop's end and stayed silent.",
          'Clicking elsewhere on the timeline while playing now picks the pattern up there, in time.',
          'A start from a pre-roll, or from between two 16ths, now lands in step with the DAW.',
        ],
      },
      {
        version: '0.2.4',
        notes: [
          'Computer keys by place now work in the plugin: 0.2.3 had left them out of the build, so German (QWERTZ) and French (AZERTY) keyboards still played the keys by their labels.',
          'Fixed: rare mistimed tom and clap transients on fast retriggers (rolls, flams, a tom hit while one still rings).',
        ],
      },
      {
        version: '0.2.3',
        notes: [
          'Computer keys go by place, as on a US keyboard: on a German (QWERTZ) or French (AZERTY) keyboard the step, track and function keys sit where the key list shows them.',
          "With the computer's Shift (SHIFT) held, the number keys now press steps 1-8, as Q to I press 9-16.",
          'Command (Ctrl on Windows) with a key now goes on to the DAW instead of pressing a panel key.',
        ],
      },
      {
        version: '0.2.2',
        notes: ["Windows: in Ableton Live the window could stop taking mouse clicks, and Live's with it. Built with JUCE 8.0.15, which fixes the window drawing behind it."],
      },
      {
        version: '0.2.1',
        notes: [
          "The ROM folder is per plugin: choosing one here no longer changes another plugin's.",
          'Until a plugin has found its ROMs or artwork, it uses the last folder chosen in any plugin.',
          'Artwork can be named after the plugin, anywhere in the ROM folder: ro-drums-909-maker.svg or .png. artwork/tr909/ still works.',
        ],
      },
      { version: '0.2.0', notes: ['The first release, a beta.'] },
    ],
  },
  {
    slug: 'ro-drums-808',
    emulates: 'Roland TR-808 Rhythm Composer',
    year: 1980,
    type: 'drum machine',
    status: 'working',
    withoutRom: 'voice',
    keywords: '808 drums drum machine rhythm composer roland',
    nav: true,
    release: { version: '0.1.5', tag: 'ro-drums-808-v0.1.5', stage: 'beta', mac: 'ro-drums-808-0.1.5-mac.pkg', windows: 'ro-drums-808-0.1.5-windows.zip' },
    changes: [
      {
        version: '0.1.6',
        notes: [
          "The kick's DECAY pot, VR6, is now the printed 500k: at full DECAY the kick falls to a tenth in 0.80 s, the service notes' figure, and kicks landing on each other's ring keep their level (0.3 dB apart at 120 BPM, was 7). Sessions saved before keep their sound.",
          "BD decay, in SERVICE under PARTS: VR6 from the printed 500k up to the measured unit's 585k (1.19 s). Under the mouse the pot is ringed on the board and the scope's caption says what it does. A dot on SERVICE shows a trimmer or the part moved off the unit's own.",
        ],
      },
      {
        version: '0.1.5',
        notes: [
          "With SYNC on HOST, the rhythm now lands on the DAW's grid in playback and bounces: the plugin reports the 4 ms its steps came after the grid as its latency, so the DAW takes it off. MIDI notes are held back to match.",
        ],
      },
      {
        version: '0.1.4',
        notes: [
          'A reopened song keeps the rhythm picked in MANUAL PLAY. Sessions now save and load the whole unit, CPU included, instead of starting it as at power-on; only the POWER switch does that. Songs saved with 0.1.3 and before start fresh once more.',
        ],
      },
      {
        version: '0.1.3',
        notes: [
          'Computer keys by place now work in the plugin: 0.1.2 had left them out of the build, so German (QWERTZ) and French (AZERTY) keyboards still played the keys by their labels.',
        ],
      },
      {
        version: '0.1.2',
        notes: [
          'Computer keys go by place, as on a US keyboard: on a German (QWERTZ) or French (AZERTY) keyboard the step keys sit where the key list shows them.',
          'Command (Ctrl on Windows) with a key now goes on to the DAW instead of pressing a panel key.',
        ],
      },
      {
        version: '0.1.1',
        notes: ['Windows: built with JUCE 8.0.15, which fixes window drawing that could stop the host taking mouse clicks (seen with ro-drums-909 in Ableton Live).'],
      },
      { version: '0.1.0', notes: ['The first release, a beta.'] },
    ],
  },
  {
    slug: 'ac-digital-vc',
    emulates: 'Access Virus C',
    year: 2002,
    type: 'virtual analog synth',
    status: 'working',
    withoutRom: 'none',
    keywords: 'virus c access virtual analog va synth dsp osirus',
    nav: true,
    release: { version: '0.1.1', tag: 'ac-digital-vc-v0.1.1', stage: 'beta', mac: 'ac-digital-vc-0.1.1-mac.pkg', windows: 'ac-digital-vc-0.1.1-windows.zip' },
    changes: [
      {
        version: '0.1.1',
        notes: ['Windows: built with JUCE 8.0.15, which fixes window drawing that could stop the host taking mouse clicks (seen with ro-drums-909 in Ableton Live).'],
      },
      { version: '0.1.0', notes: ['The first release, a beta.'] },
    ],
  },
  {
    slug: 'al-fx-quadgt',
    emulates: 'Alesis QuadraVerb GT',
    year: 1991,
    type: 'guitar preamp and multi-effects',
    status: 'working',
    withoutRom: 'none',
    keywords: 'quadraverb quadgt gt alesis guitar preamp amp distortion overdrive cabinet reverb delay chorus multi effects',
    nav: true,
    release: { version: '0.1.2', tag: 'al-fx-quadgt-v0.1.2', stage: 'beta', mac: 'al-fx-quadgt-0.1.2-mac.pkg', windows: 'al-fx-quadgt-0.1.2-windows.zip' },
    changes: [
      {
        version: '0.1.2',
        notes: [
          'The key list (?) was unreadable: the panel is too short for it in one column. It now shows in two.',
          'On a mono track it now takes mono in, stereo out, the manual\'s "Mono In, Stereo Out". Mono in, mono out gives both outputs mixed, as the unit does with one output jack plugged in; it gave the right output alone.',
        ],
      },
      {
        version: '0.1.1',
        notes: [
          'Firmware v1.00 (qvgtv100.bin) works too, when v1.03 isn\'t in the ROM folder: the first factory set. A session keeps the memory of the firmware it was saved with; opened with the other, the unit starts with that firmware\'s factory programs and the session keeps the saved memory for later.',
          'The older hd44780_a00.bin many ROM sets have, drawn from the datasheet (CRC 01d108e2), is taken too, with its six wrong characters corrected. It was reported as not found.',
        ],
      },
      { version: '0.1.0', notes: ['The first release, a beta.'] },
    ],
  },
  {
    slug: 'ro-mono-101',
    emulates: 'Roland SH-101',
    year: 1982,
    type: 'mono synth',
    status: 'working',
    withoutRom: 'voice',
    keywords: '101 sh101 sh-101 mono synth bass lead arpeggio sequencer roland',
    nav: true,
    release: { version: '0.1.4', tag: 'ro-mono-101-v0.1.4', stage: 'beta', mac: 'ro-mono-101-0.1.4-mac.pkg', windows: 'ro-mono-101-0.1.4-windows.zip' },
    changes: [
      {
        version: '0.1.4',
        notes: [
          "With Sync to Host on, the arpeggio and sequence now land within about 1.4 ms of the DAW's grid in playback and bounces (the arpeggio just before, the sequence just after: the firmware steps them a CPU loop apart): the plugin reports 3.5 ms of firmware lag with its latency. MIDI notes are held back to match.",
        ],
      },
      {
        version: '0.1.3',
        notes: [
          'A reopened song keeps ARPEGGIO, HOLD and the held keys. Sessions now save and load the whole unit instead of starting it as at power-on; only POWER does that. A held arpeggio with Sync to Host off plays as soon as the song opens, as a 101 left on would. Songs saved with 0.1.2 and before start fresh once more.',
        ],
      },
      {
        version: '0.1.2',
        notes: [
          'Computer keys by place now work in the plugin: 0.1.1 had left them out of the build, so German (QWERTZ) and French (AZERTY) keyboards still played the keys by their labels.',
          "Fixed: FREQ near the top with RES high made white noise instead of the filter's whistle.",
          "FREQ's top range now matches real units: at FREQ 10 the filter self-oscillates around 14.7 kHz (it was 28 kHz), and above about FREQ 8 it sits up to an octave lower. Sessions saved with FREQ high sound darker; move FREQ up for the old brightness.",
        ],
      },
      {
        version: '0.1.1',
        notes: [
          'Computer keys go by place, as on a US keyboard: on a German (QWERTZ) or French (AZERTY) keyboard the note keys and Z X (octave) sit where the key list shows them.',
          'Command (Ctrl on Windows) with a key now goes on to the DAW instead of pressing a panel key.',
        ],
      },
      { version: '0.1.0', notes: ['The first release, a beta.'] },
    ],
  },
  {
    slug: 'pr-dist-r',
    emulates: 'ProCo RAT',
    year: 1984,
    type: 'distortion pedal',
    status: 'working',
    withoutRom: 'not-needed',
    standalone: false,
    keywords: 'rat proco pro co distortion pedal white face lm308 guitar 303 acid fuzz overdrive effect',
    nav: true,
    release: { version: '0.1.0', tag: 'pr-dist-r-v0.1.0', stage: 'beta', mac: 'pr-dist-r-0.1.0-mac.pkg', windows: 'pr-dist-r-0.1.0-windows.zip' },
    changes: [{ version: '0.1.0', notes: ['The first release, a beta.'] }],
  },
  {
    slug: 'ob-poly-xp',
    emulates: 'Oberheim Xpander',
    year: 1984,
    type: 'poly synth module',
    status: 'working',
    withoutRom: 'none',
    keywords: 'xpander oberheim matrix-12 matrix 12 poly polyphonic analog six voice module cem3372 cem3374 filter modes fm modulation',
    nav: true,
    release: { version: '0.1.0', tag: 'ob-poly-xp-v0.1.0', stage: 'beta', mac: 'ob-poly-xp-0.1.0-mac.pkg', windows: 'ob-poly-xp-0.1.0-windows.zip' },
    changes: [{ version: '0.1.0', notes: ['The first release, a beta.'] }],
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
// accepts the source offered from the same place. Each release is tagged there (Release.tag).
export const sourceOf = (r: Release) => `https://github.com/lemus-dev/lemus/tree/${r.tag}`;

// The installers are attached to the release on GitHub (its tag), which counts their downloads.
// public/_redirects sends the old /download/<file> links there.
export const assetOf = (r: Release, file: string) =>
  `https://github.com/lemus-dev/lemus/releases/download/${r.tag}/${file}`;

export const downloadLinks = (m: Machine) =>
  m.release
    ? { mac: assetOf(m.release, m.release.mac), windows: assetOf(m.release, m.release.windows) }
    : { mac: '/download', windows: '/download' };

// Each system the plugins run on, and the formats for it.
export const platforms: [string, string][] = [
  ['macOS 11+', 'Universal: AU, VST3, standalone'],
  ['Windows 10+', 'VST3, standalone'],
];
