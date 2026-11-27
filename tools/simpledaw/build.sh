#!/bin/bash
# Builds "build/Simple DAW.app": the Objective-C core with clang, the app with swiftc.
#   ./build.sh            build
#   ./build.sh --open     build and (re)start it
set -euo pipefail
cd "$(dirname "$0")"
APP="build/Simple DAW.app"
FONTS=../../third_party/fonts
MIN=14.0

mkdir -p build/obj "$APP/Contents/MacOS" "$APP/Contents/Resources"
for m in Sources/*.m; do
	clang -c -fobjc-arc -O2 -g -Wall -Wno-unused-parameter -mmacosx-version-min=$MIN "$m" -o "build/obj/$(basename "$m" .m).o"
done
swiftc -O -g -swift-version 5 -target arm64-apple-macos$MIN -import-objc-header Sources/Bridge.h \
	-suppress-warnings Sources/*.swift build/obj/*.o \
	-framework AppKit -framework AudioToolbox -framework CoreAudioKit -framework CoreMIDI \
	-o "$APP/Contents/MacOS/Simple DAW"
cp Info.plist "$APP/Contents/Info.plist"
cp "$FONTS/JetBrainsMono-Regular.ttf" "$FONTS/JetBrainsMono-Bold.ttf" "$APP/Contents/Resources/"
if [ ! -f build/AppIcon.icns ] || [ icon.svg -nt build/AppIcon.icns ]; then
	rm -rf build/AppIcon.iconset && mkdir build/AppIcon.iconset
	for s in 16 32 128 256 512; do
		rsvg-convert -w $s -h $s icon.svg -o build/AppIcon.iconset/icon_${s}x${s}.png
		rsvg-convert -w $((s * 2)) -h $((s * 2)) icon.svg -o build/AppIcon.iconset/icon_${s}x${s}@2x.png
	done
	iconutil -c icns build/AppIcon.iconset -o build/AppIcon.icns
fi
cp build/AppIcon.icns "$APP/Contents/Resources/AppIcon.icns"
codesign -s - --force "$APP" 2>/dev/null
echo "built $APP"

if [ "${1:-}" = "--open" ]; then
	pkill -x "Simple DAW" 2>/dev/null && sleep 0.5 || true
	open "$APP"
fi
