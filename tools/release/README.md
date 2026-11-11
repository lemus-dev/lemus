# Releases

A release is each shipping machine's own plugin (`-DLEMUS_MACHINES`, for now `tb303;tr909;tr808;virusc`),
each a download of its own: the TB-303 is **ro-mono-303**, the TR-909 **ro-drums-909**, the TR-808 **ro-drums-808**, the Virus C **ac-digital-vc**. A machine's plugin is the host with that
machine alone, declared in its `machine.cmake` (`lemus_plugin`: name, plugin code, description,
whether it has a standalone app, its version). The all-in-one Lemus and Lemus FX don't ship. Each
plugin has its own version (`lemus_plugin(... VERSION x.y.z)`) and is released on its own: a
release raises the versions of the plugins in it and builds only those. Each release's source is
published at https://github.com/lemus-dev/lemus (AGPLv3), tagged `<plugin>-v<version>`
(`ro-drums-808-v0.1.0`); a release of several plugins tags the same commit once per plugin. Up to
0.2.0 every plugin shared the project's version, and those releases are tagged `v<version>`.

| Platform | Formats | Built by | Output |
|---|---|---|---|
| macOS 11+, universal | AU, VST3, standalone app | `tools/release/mac.sh`, locally | `dist/<plugin>-<version>-mac.pkg` per plugin |
| Windows 10+, x64 | VST3, standalone app | `.github/workflows/release.yml` (GitHub Actions; the TR-909, TR-808 and Virus C with clang-cl) | `<plugin>-<version>-windows.zip` per plugin, the run's artifact |

Both include `LICENSE.txt` (the AGPLv3, which must go with every copy) and `NOTICES.txt` (JUCE, MAME's uCOM-4, uPD7810, MCS-51 and HD44780, the dsp56300 emulator with the GPLv3's text, asmjit, JetBrains Mono, IBM Plex Mono, Michroma). The Mac installer puts the
AU and VST3 in `/Library/Audio/Plug-Ins` and the app in `/Applications`; the Windows zip has a
readme (`windows-readme.txt`) that says where the VST3 goes.

## Making one

1. Set each released plugin's version in its `machines/<id>/machine.cmake` (`lemus_plugin(...
   VERSION x.y.z)`). Sessions record it (`savedBy`), and the update check compares it with the
   website's for that plugin.
2. Run the checks: each released plugin's (`lemustest_tb303`, `lemustest_tr909`),
   `lemustest_instrument` and each released machine's own (`tb303plugintest`, `tr909plugintest`),
   with the private folder in place, and `auval -v aumu R303 Lmus` / `auval -v aumu R909 Lmus` on
   the installed AUs (each plugin's code is in its `machine.cmake`). There are no manual DAW
   checks before a release.
3. macOS: `tools/release/mac.sh` with `LEMUS_MACHINES` set to the released machines
   (`LEMUS_MACHINES="tb303;tr808"`) and `LEMUS_SIGN_APP`, `LEMUS_SIGN_INSTALLER` and
   `LEMUS_NOTARY_PROFILE` set (see the script). Without the signing settings it builds an unsigned
   package for testing.
4. Windows: tag the commit `<plugin>-v<version>` for each released plugin and push the tags (or
   run the workflow by hand); download the zips from the runs. A tag's run keeps only that plugin's
   zip, and fails if the plugin's VERSION isn't the tag's. The workflow lists the plugins it
   builds and tests. Once SignPath is set up (below), take them from the public repo's runs for the
   tags instead (step 5 pushes them there): those are signed.
5. Publish the release's source to https://github.com/lemus-dev/lemus with the same tags
   (`tools/publish/publish.sh -t ro-mono-303-v0.2.1 -t ro-drums-808-v0.1.0 "..."`): the binaries
   are AGPLv3, so their source must be out when they are.
6. Write what the version changed as a `changes` entry for each plugin in
   `website/src/data/machines.ts` (short lines; the plugin's what's-new window shows them), and
   use the same lines as the release notes. Attach each plugin's installers to a GitHub release of
   its tag, which counts their downloads:

       gh release create ro-mono-303-v<version> -R lemus-dev/lemus --verify-tag --latest \
         --title "ro-mono-303 <version>" --notes "<changes>" dist/ro-mono-303-<version>-*

7. Set each released plugin's `release` (version, tag, file names) in
   `website/src/data/machines.ts` and run
   `npm run deploy` in `website/`. That publishes the download links (to the GitHub release's
   files) and `latest.json`, so it's the last step: every open plugin window shows the new version
   within a day, and pops up what's new in it. The site builds `latest.json` from the same list:

       {"plugins": {"ro-mono-303": {"version": "0.2.0", "url": "https://lemusdev.org/ro-mono-303",
                                    "changes": [{"version": "0.2.0", "notes": ["..."]}]}}}

8. Announce it, once the site has it. The plugins' what's-new window and the GitHub release
   reach users on their own; besides those:
   - KVR: set the product's Windows and macOS versions (kvraudio.com/z/dashboard/lemus).
   - The forum: a post in Announcements on https://lemus.discourse.group, with the changes
     and the plugin's page.
   - X (@lemusdev): a short post with the plugin's page.
   - The mailing list (Kit, the "Lemus releases" form), for a release worth an email.
   - Whoever reported what it fixes: their GitHub issue, forum topic or email.

## Signing on Windows

SignPath Foundation signs the Windows zips for free, as an open-source project: the workflow's run
in the public repo, where the secrets `SIGNPATH_API_TOKEN` and `SIGNPATH_ORGANIZATION_ID` are set,
uploads each zip, an approver approves the request on signpath.io within the hour, and the signed
zip becomes the run's artifact. SignPath's project is `lemus`, its policy `release-signing`, its
artifact configuration `tools/release/signpath.xml`. Their rules (https://signpath.org/terms):
every signed file's product name is Lemus (`host/version.rc.in`), the site publishes a code signing
policy (`/code-signing`, linked from every page), and the team uses two-factor sign-in on GitHub
and SignPath. The certificate is SignPath Foundation's, so Windows names it as the publisher.

## The update check

`host/Updates.h`. While a plugin's window is open, once a day, the plugin reads `latest.json` from
the website (`LEMUS_SITE`, https://lemusdev.org), sending nothing but the request itself (no query,
no cookies, a fixed `User-Agent: Lemus`), and looks up its own name in it: a plugin the file
doesn't list shows nothing. A newer version shows in the bar and in a what's-new window with the
notes of every version since the installed one: Download, Remind me later (a week) or Skip this
version (until the next). The About box can switch it all off. The test programs never go online.
