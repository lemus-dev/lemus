#!/bin/bash
# Builds the macOS release of Lemus Plugin Manager (manager/README.md): the app, universal (arm64
# and x86_64), in an installer that puts it in /Applications:
# dist/lemus-plugin-manager-<version>-mac.pkg, the version the app's own (LEMUS_MANAGER_VERSION in
# manager/manager.cmake, read back from the built bundle).
#
#   tools/release/manager-mac.sh
#
# Signing and notarisation as in tools/release/mac.sh: LEMUS_SIGN_APP, LEMUS_SIGN_INSTALLER,
# LEMUS_NOTARY_PROFILE, each skipped with a warning when it isn't set.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=$ROOT/build/release-manager
DIST=$ROOT/dist

# The manager needs no machine; one keeps the configure step small.
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
	-DLEMUS_MACHINES=tb303 -DLEMUS_TOOLS=OFF -DLEMUS_COPY_PLUGINS=OFF
cmake --build "$BUILD" -j --target lemus_manager lemustest_manager
"$BUILD/lemustest_manager_artefacts/Release/lemustest_manager"

app="$BUILD/lemus_manager_artefacts/Release/Lemus Plugin Manager.app"
archs=$(lipo -archs "$app/Contents/MacOS/Lemus Plugin Manager")
[[ $archs == *arm64* && $archs == *x86_64* ]] || { echo "not universal: $app ($archs)"; exit 1; }
VERSION=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$app/Contents/Info.plist")

mkdir -p "$app/Contents/Resources"
cp "$ROOT/tools/release/NOTICES.txt" "$app/Contents/Resources/"
cp "$ROOT/LICENSE" "$app/Contents/Resources/LICENSE.txt"

if [[ -n ${LEMUS_SIGN_APP:-} ]]; then
	codesign --force --options runtime --timestamp --sign "$LEMUS_SIGN_APP" "$app"
	codesign --verify --strict "$app"
else
	echo "warning: LEMUS_SIGN_APP isn't set: the app is unsigned"
fi

stage=$BUILD/stage
rm -rf "$stage" && mkdir -p "$stage/root" "$stage/pkgs"
cp -R "$app" "$stage/root/"
xattr -cr "$stage"
pkgbuild --analyze --root "$stage/root" "$stage/app.plist" >/dev/null
plutil -replace 0.BundleIsRelocatable -bool NO "$stage/app.plist"
pkgbuild --root "$stage/root" --component-plist "$stage/app.plist" --install-location /Applications \
	--identifier org.lemusdev.pluginmanager.app --version "$VERSION" "$stage/pkgs/app.pkg"

mkdir -p "$DIST"
pkg=$DIST/lemus-plugin-manager-$VERSION-mac.pkg
sign=()
[[ -n ${LEMUS_SIGN_INSTALLER:-} ]] && sign=(--sign "$LEMUS_SIGN_INSTALLER")
productbuild ${sign[@]+"${sign[@]}"} --identifier org.lemusdev.pluginmanager --version "$VERSION" --package "$stage/pkgs/app.pkg" "$pkg"
[[ -z ${LEMUS_SIGN_INSTALLER:-} ]] && echo "warning: LEMUS_SIGN_INSTALLER isn't set: $(basename "$pkg") is unsigned"

if [[ -n ${LEMUS_NOTARY_PROFILE:-} ]]; then
	xcrun notarytool submit "$pkg" --keychain-profile "$LEMUS_NOTARY_PROFILE" --wait
	xcrun stapler staple "$pkg"
else
	echo "warning: LEMUS_NOTARY_PROFILE isn't set: $(basename "$pkg") isn't notarised"
fi
echo "$pkg"
