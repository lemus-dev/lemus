#!/bin/bash
# Builds the macOS release: each shipping machine's own plugin (AU, VST3 and, where it has one, the
# standalone app), universal (arm64 and x86_64), each in an installer of its own:
# dist/<plugin>-<version>-mac.pkg (dist/ro-mono-303-0.1.0-mac.pkg).
#
#   tools/release/mac.sh
#
# Signing and notarisation happen when their settings are there, and are skipped (with a warning)
# when they aren't, so an unsigned build works for testing:
#   LEMUS_SIGN_APP        "Developer ID Application: ..." (signs the plugins and the app)
#   LEMUS_SIGN_INSTALLER  "Developer ID Installer: ..."  (signs the .pkg)
#   LEMUS_NOTARY_PROFILE  a notarytool keychain profile (xcrun notarytool store-credentials)
# LEMUS_MACHINES overrides the machines that ship, by id, separated by ; (default: tb303).
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
MACHINES=${LEMUS_MACHINES:-tb303}
BUILD=$ROOT/build/release-mac
DIST=$ROOT/dist
VERSION=$(sed -n 's/^project(LEMUS VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
	-DLEMUS_MACHINES="$MACHINES" -DLEMUS_TOOLS=OFF -DLEMUS_COPY_PLUGINS=OFF
targets=()
for id in ${MACHINES//;/ }; do
	targets+=("plugin_${id}_All")
done
cmake --build "$BUILD" -j --target "${targets[@]}"

# One component package per install location, so each lands in place; not relocatable, or the
# installer would update a copy of the bundle it finds elsewhere (a development build) instead.
component() { # stage id name root install-location
	pkgbuild --analyze --root "$1/$4" "$1/$3.plist" >/dev/null
	plutil -replace 0.BundleIsRelocatable -bool NO "$1/$3.plist"
	pkgbuild --root "$1/$4" --component-plist "$1/$3.plist" --install-location "$5" \
		--identifier "org.lemusdev.$2.$3" --version "$VERSION" "$1/pkgs/$3.pkg"
}

package() { # id
	local id=$1 art=$BUILD/plugin_$1_artefacts/Release
	local au
	au=$(echo "$art"/AU/*.component)
	local name
	name=$(basename "$au" .component)
	local vst3=$art/VST3/$name.vst3 app=$art/Standalone/$name.app
	local bundles=("$au" "$vst3")
	[[ -d $app ]] && bundles+=("$app")

	for b in "${bundles[@]}"; do
		archs=$(lipo -archs "$b/Contents/MacOS/$name")
		[[ $archs == *arm64* && $archs == *x86_64* ]] || { echo "not universal: $b ($archs)"; exit 1; }
	done

	# The licence and the notices go with every bundle: the AGPL asks for its text with every copy of
	# the binaries, the BSD licence for its notice.
	for b in "${bundles[@]}"; do
		mkdir -p "$b/Contents/Resources"
		cp "$ROOT/tools/release/NOTICES.txt" "$b/Contents/Resources/"
		cp "$ROOT/LICENSE" "$b/Contents/Resources/LICENSE.txt"
	done

	if [[ -n ${LEMUS_SIGN_APP:-} ]]; then
		for b in "${bundles[@]}"; do
			codesign --force --options runtime --timestamp --sign "$LEMUS_SIGN_APP" "$b"
			codesign --verify --strict "$b"
		done
	else
		echo "warning: LEMUS_SIGN_APP isn't set: $name is unsigned"
	fi

	local stage=$BUILD/stage/$id
	rm -rf "$stage" && mkdir -p "$stage"/{au,vst3,app,pkgs}
	cp -R "$au" "$stage/au/"
	cp -R "$vst3" "$stage/vst3/"
	xattr -cr "$stage" # no extended attributes, or the packages carry ._ files
	component "$stage" "$id" au au /Library/Audio/Plug-Ins/Components
	component "$stage" "$id" vst3 vst3 /Library/Audio/Plug-Ins/VST3
	local pkgs=(--package "$stage/pkgs/au.pkg" --package "$stage/pkgs/vst3.pkg")
	if [[ -d $app ]]; then
		cp -R "$app" "$stage/app/"
		xattr -cr "$stage/app"
		component "$stage" "$id" app app /Applications
		pkgs+=(--package "$stage/pkgs/app.pkg")
	fi

	mkdir -p "$DIST"
	local pkg=$DIST/$name-$VERSION-mac.pkg
	local sign=()
	[[ -n ${LEMUS_SIGN_INSTALLER:-} ]] && sign=(--sign "$LEMUS_SIGN_INSTALLER")
	productbuild ${sign[@]+"${sign[@]}"} --identifier "org.lemusdev.$id" --version "$VERSION" "${pkgs[@]}" "$pkg"
	[[ -z ${LEMUS_SIGN_INSTALLER:-} ]] && echo "warning: LEMUS_SIGN_INSTALLER isn't set: $(basename "$pkg") is unsigned"

	if [[ -n ${LEMUS_NOTARY_PROFILE:-} ]]; then
		xcrun notarytool submit "$pkg" --keychain-profile "$LEMUS_NOTARY_PROFILE" --wait
		xcrun stapler staple "$pkg"
	else
		echo "warning: LEMUS_NOTARY_PROFILE isn't set: $(basename "$pkg") isn't notarised"
	fi
	echo "$pkg"
}

for id in ${MACHINES//;/ }; do
	package "$id"
done
