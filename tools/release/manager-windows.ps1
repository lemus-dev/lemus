# Zips the Windows release of Lemus Plugin Manager (manager/README.md) from a Release build:
# dist/lemus-plugin-manager-<version>-windows.zip with the app, the licence, the notices and a
# readme. The version is the app's own, read back from the built exe's version info.
#
#   tools/release/manager-windows.ps1 -Build build/release-win
param([Parameter(Mandatory)] [string] $Build)
$ErrorActionPreference = 'Stop'

$root = Resolve-Path "$PSScriptRoot/../.."
$exe = Get-Item "$Build/lemus_manager_artefacts/Release/Lemus Plugin Manager.exe"
$version = $exe.VersionInfo.ProductVersion
if ($version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') { throw "Lemus Plugin Manager.exe: no version in its version info ('$version')" }

$stage = "$Build/stage-manager/lemus-plugin-manager-$version"
Remove-Item -Recurse -Force "$Build/stage-manager" -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item $exe.FullName $stage
Copy-Item "$root/tools/release/NOTICES.txt" $stage
Copy-Item "$root/LICENSE" "$stage/LICENSE.txt"
Set-Content -NoNewline -Path "$stage/README.txt" -Value @"
Lemus Plugin Manager for Windows
================================

Lemus Plugin Manager.exe   lists the released Lemus plugins and installs, updates and removes
                           them. Put it anywhere and run it. Windows asks once for permission
                           each time it installs or removes.

It installs each plugin's VST3 to C:\Program Files\Common Files\VST3 and its standalone app to
C:\Program Files\Lemus\<plugin>, with a Start menu shortcut, from the same zip the plugin's page
offers, after checking the download's SHA-256. The plugins don't need it: each still has its own
download.

More at https://lemusdev.org

Machine names identify the original equipment and are trademarks of their owners. Lemus is not
affiliated with or endorsed by them.

Lemus is free software under the GNU AGPLv3 (LICENSE.txt); its source is at
https://github.com/lemus-dev/lemus. Third-party notices: NOTICES.txt.
"@

New-Item -ItemType Directory -Force "$root/dist" | Out-Null
$zip = "$root/dist/lemus-plugin-manager-$version-windows.zip"
Remove-Item -Force $zip -ErrorAction SilentlyContinue
Compress-Archive -Path $stage -DestinationPath $zip
Write-Output $zip
