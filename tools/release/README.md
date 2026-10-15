# Releases

A release is each shipping machine's own plugin (`-DLEMUS_MACHINES`, for now `tb303`), each a
download of its own: the TB-303 is **ro-mono-303**. A machine's plugin is the host with that
machine alone, declared in its `machine.cmake` (`lemus_plugin`: name, plugin code, description,
whether it has a standalone app). The all-in-one Lemus and Lemus FX don't ship. Every plugin
shares one version (the project's), so a release rebuilds them all. Each release's source is
published at https://github.com/lemus-dev/lemus (AGPLv3), tagged `v<version>`.

| Platform | Formats | Built by | Output |
|---|---|---|---|
| macOS 11+, universal | AU, VST3, standalone app | `tools/release/mac.sh`, locally | `dist/<plugin>-<version>-mac.pkg` per plugin |
| Windows 10+, x64 | VST3, standalone app | `.github/workflows/release.yml` (GitHub Actions) | `<plugin>-<version>-windows.zip` per plugin, the run's artifact |

Both include `NOTICES.txt` (JUCE, MAME's uCOM-4 core, JetBrains Mono, IBM Plex Mono). The Mac installer puts the
AU and VST3 in `/Library/Audio/Plug-Ins` and the app in `/Applications`; the Windows zip has a
readme (`windows-readme.txt`) that says where the VST3 goes.

## Making one

1. Set the version in `CMakeLists.txt` (`project(LEMUS VERSION x.y.z)`). Sessions record it
   (`savedBy`), and the update check compares it with the website's.
2. Run the checks: each shipping plugin's (`lemustest_tb303`), `lemustest_instrument` and each
   shipping machine's own (`tb303plugintest`), with the private folder in place.
3. macOS: `tools/release/mac.sh` with `LEMUS_SIGN_APP`, `LEMUS_SIGN_INSTALLER` and
   `LEMUS_NOTARY_PROFILE` set (see the script). Without them it builds an unsigned package for
   testing.
4. Windows: tag the commit `v<version>` and push it (or run the workflow by hand); download the
   zips from the run. The workflow lists the plugins it builds and tests.
5. Try both installers on a clean machine, then the DAW checks below.
6. Publish the release's source to https://github.com/lemus-dev/lemus and tag it `v<version>`
   there too: the binaries are AGPLv3, so their source must be out when they are.
7. Upload them to the website, then set each released plugin's `release` in
   `website/src/data/machines.ts` and run `npm run deploy` in `website/`. That publishes the
   download links and `latest.json`, so it's the last step: every open plugin window shows the new
   version within a day. The site builds `latest.json` from the same list:

       {"plugins": {"ro-mono-303": {"version": "0.2.0", "url": "https://lemusdev.org/ro-mono-303"}}}

## DAW checks

In each host: insert the plugin (it opens with its machine), play it from MIDI, run its own sequencer with the
host's transport (SYNC on), automate a knob, save, close and reopen the project (the sound,
knobs and patterns come back), check that the automation lanes show the machine's parameter
names, and remove the instance.

- macOS: Logic Pro (AU), Ableton Live (AU and VST3), Reaper (AU and VST3), the standalone app.
  `auval -v aumu R303 Lmus` passes (the plugin's code is in its `machine.cmake`).
- Windows: Ableton Live, Reaper, FL Studio or Bitwig (VST3), the standalone app.
- Without the ROM: the 303 loads in voice mode and plays from MIDI.

## The update check

`host/Updates.h`. While a plugin's window is open, once a day, the plugin reads `latest.json` from
the website (`LEMUS_SITE`, https://lemusdev.org), sending nothing but the request itself (no query,
no cookies, a fixed `User-Agent: Lemus`), and looks up its own name in it: a plugin the file
doesn't list shows nothing. The About box can switch it off. The test programs never go online.
