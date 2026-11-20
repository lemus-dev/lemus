# Lemus Plugin Manager

An app that lists the released plugins and installs, updates and removes them. It is optional:
every plugin still has its own download, and the plugins never need it.

It reads the list from `https://lemusdev.org/latest.json`, the file the plugins' update check
reads (`website/src/pages/latest.json.ts` builds it). Each entry there carries the machine's name,
year and kind, and its installers with their SHA-256 and size, taken from the GitHub release when
the site is built.

It installs from the same files the download pages offer:

- **macOS**: the release's `.pkg`, through `/usr/sbin/installer`, so the receipts are the same as
  a manual install. The formats not wanted are switched off with `-applyChoiceChangesXML`; an
  update keeps the formats installed. Removing deletes the bundles and forgets their receipts.
- **Windows**: the release's `.zip`. The VST3 goes to `C:\Program Files\Common Files\VST3`, the
  app with its licence and notices to `C:\Program Files\Lemus\<plugin>`, and a shortcut to the
  Start menu.

Plugins are installed for every user, where every DAW looks: `/Library/Audio/Plug-Ins` and
`/Applications` on macOS. Every download is checked against its SHA-256 before anything is
installed; one that doesn't match is thrown away and the installed version stays. A batch (Update
all, or one plugin) asks for the password once: on macOS a shell script runs through `osascript`
with administrator privileges; on Windows the app starts itself again elevated (`--apply <plan>`).
Without a Developer ID there is no privileged helper, so each batch asks.

What runs with those rights trusts as little as it can:

- The catalogue's plugin names and versions must be plain (`ro-mono-303`, `0.2.5`); an entry
  with any other is skipped, since names become file paths and words in the root script.
- macOS: the script goes to `osascript` as an argument, not a file. As root it copies each
  download into a folder only root can write, checks its SHA-256 again there and installs that
  copy, so nothing left in the user's temporary folder is trusted after the first check.
- Windows: the elevated copy checks every step of the plan. It writes only the plugin's own VST3,
  app folder and Start menu shortcut, and copies only from the plan's folder. The exe loads DLLs
  from System32 only (`/DEPENDENTLOADFLAG` and `SetDefaultDllDirectories`), never from the folder
  it was started in.

A copy in the user's own `~/Library/Audio/Plug-Ins` (a development build) isn't the manager's;
it's shown in the details, since a DAW may load either copy.

Nothing is reported anywhere: when something goes wrong the user gets what helps to report it.
A failed job, or the manager's own update, has Copy details (the version and system, what it was
doing, why it failed, the installer's log). A crash writes its stack to `Plugin Manager crash.txt`
in the Lemus settings folder; the next launch says so, offers to copy it and the forum, and
deletes it (`Report.h`).

Requests to lemusdev.org use the user agent `Lemus Plugin Manager`, not the plugins' `Lemus`, so
the site's count of plugin installs stays the plugins'.

## Build and check

    cmake -S . -B build/manager -DLEMUS_MACHINES=tb303 -DLEMUS_TOOLS=OFF -DLEMUS_COPY_PLUGINS=OFF
    cmake --build build/manager -j --target lemus_manager lemustest_manager
    build/manager/lemustest_manager_artefacts/Release/lemustest_manager

The checks cover the catalogue, version reading, the installer's choices, the Windows plan, and a
whole batch (download, SHA-256 check, install, remove) under a test root, from
`dist/ro-mono-303-0.2.4-mac.pkg` when it's there.

The app takes, for testing:

    --catalog <file or URL>   the list from there instead of the website
    --root <folder>           install under this folder; nothing asks for a password
    --snapshot <png> [--select <plugin>] [--action update-all|update|install|remove] [--wait <ms>]
                              draw the window to a PNG once the list is read

## Release

`tools/release/manager-mac.sh` builds `dist/lemus-plugin-manager-<version>-mac.pkg` (universal, the
app into `/Applications`). The Windows zip comes from the release workflow. Then
`website/src/data/manager.ts` gets the release, and `/latest.json` lists the manager.

Older copies then update themselves, as the other plugin managers do: on launch a card offers the
new version (Later, or Update now; the footer's pill brings it back). The installer is downloaded
and checked like a plugin's and installed (macOS: the pkg as root, one password; Windows: the new
exe takes this one's place, the old one renamed `<exe>.old` and removed on the next start). The
new version then starts and waits for the old one to quit (`--after <pid>`). Plugins never update
without a click.
