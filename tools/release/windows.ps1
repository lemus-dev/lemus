# Zips the Windows release from a Release build (see .github/workflows/release.yml): each machine's
# own plugin built there in a zip of its own, dist/<plugin>-<version>-windows.zip
# (dist/ro-mono-303-0.1.0-windows.zip), with the VST3, the standalone app if it has one, the
# licence (the AGPL asks for its text with every copy), the notices and a readme.
#
#   tools/release/windows.ps1 -Build build/release-win
param([Parameter(Mandatory)] [string] $Build)
$ErrorActionPreference = 'Stop'

$root = Resolve-Path "$PSScriptRoot/../.."
$version = (Select-String -Path "$root/CMakeLists.txt" -Pattern 'project\(LEMUS VERSION ([0-9.]+)').Matches[0].Groups[1].Value
New-Item -ItemType Directory -Force "$root/dist" | Out-Null
Remove-Item -Recurse -Force "$Build/stage" -ErrorAction SilentlyContinue

foreach ($art in Get-ChildItem -Directory "$Build/plugin_*_artefacts") {
	$vst3 = Get-ChildItem -Directory "$($art.FullName)/Release/VST3/*.vst3"
	$name = [IO.Path]::GetFileNameWithoutExtension($vst3.Name) # a folder's BaseName keeps .vst3
	$stage = "$Build/stage/$name-$version"
	New-Item -ItemType Directory -Force $stage | Out-Null
	Copy-Item -Recurse $vst3.FullName $stage
	$app = "$($art.FullName)/Release/Standalone/$name.exe"
	$readme = (Get-Content -Raw "$root/tools/release/windows-readme.txt").Replace('{NAME}', $name)
	if (Test-Path $app) {
		Copy-Item $app $stage
	} else {
		# No standalone app: drop its line from the readme.
		$readme = ($readme -split "`n" | Where-Object { $_ -notmatch "^$([regex]::Escape($name))\.exe" }) -join "`n"
	}
	Set-Content -NoNewline -Path "$stage/README.txt" -Value $readme
	Copy-Item "$root/tools/release/NOTICES.txt" $stage
	Copy-Item "$root/LICENSE" "$stage/LICENSE.txt"

	$zip = "$root/dist/$name-$version-windows.zip"
	Remove-Item -Force $zip -ErrorAction SilentlyContinue
	Compress-Archive -Path $stage -DestinationPath $zip
	Write-Output $zip
}
