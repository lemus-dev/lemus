#!/bin/bash
# Writes website/src/data/roms.json: what each shipping plugin needs from the ROM folder (its ROMs
# and artwork places), from the machines' own specs (manager/specs.cpp), for Lemus Plugin Manager.
# Run it when a machine's ROMs or artwork places change, or a machine ships:
#   tools/release/roms-json.sh "tb303;tr909;tr808;virusc;qvgt;sh101;rat"
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=$ROOT/build/specs
MACHINES=${1:?the shipping machines, e.g. "tb303;tr909;tr808;virusc;qvgt;sh101;rat"}

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DLEMUS_MACHINES="$MACHINES" -DLEMUS_TOOLS=OFF -DLEMUS_COPY_PLUGINS=OFF >/dev/null
parts=()
for host in instrument fx; do
	if cmake --build "$BUILD" -j --target "lemus_specs_$host" >/dev/null 2>&1; then
		parts+=("$("$BUILD/lemus_specs_${host}_artefacts/Release/lemus_specs_$host")")
	fi
done
printf '%s\n' "${parts[@]}" | python3 -I -c '
import json, sys
text = sys.stdin.read()
dec, merged, i = json.JSONDecoder(), {}, 0
while i < len(text):
    while i < len(text) and text[i].isspace():
        i += 1
    if i == len(text):
        break
    obj, i = dec.raw_decode(text, i)
    merged.update(obj["plugins"])
print(json.dumps({"plugins": dict(sorted(merged.items()))}, indent=2))
' > "$ROOT/website/src/data/roms.json"
echo "$ROOT/website/src/data/roms.json"
