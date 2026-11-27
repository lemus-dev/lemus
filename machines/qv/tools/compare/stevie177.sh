#!/bin/sh
# The line-recorded QuadraVerb in YouTube py39bK-rA74 (private/qv/reference/recordings/youtube):
# a drum machine's dry passes, then the unit on factory programs. Each section against the model
# fed the dry pass that matches it (tools/compare/compare.py; the starts found by cross-correlating
# onset envelopes, docs/improvements.md, accuracy round 1). Programs 16, 11 and 12 (long halls and
# rooms) are left out: their wet fills the gaps between hits, so the fit can't tell the two paths
# apart.
#   tools/compare/stevie177.sh [WORK]
set -e
cd "$(dirname "$0")/../../../.."
WORK=${1:-$(mktemp -d)}
REC=$(ls ../private/qv/reference/recordings/youtube/py39bK-rA74*.m4a)
[ -f "$WORK/stevie177.wav" ] || ffmpeg -loglevel error -y -i "$REC" -ac 2 -ar 44100 "$WORK/stevie177.wav"
PY=.venv/bin/python
for spec in "LVP 19 8.0 359.298 11.0 3.0" "SR 10 75.7 108.927 10.8 0" "MVP 18 75.7 198.745 11.6 0" \
            "GPV 27 235.5 268.725 12.0 0" "SVP 17 8.0 337.147 11.0 0" "MVP2 18 235.5 348.410 12.2 0"; do
	set -- $spec
	echo "== $1 (program $2)"
	$PY machines/qv/tools/compare/compare.py "$WORK/stevie177.wav" "$3" "$4" "$5" "$2" --name "$1" --tail "$6" --work "$WORK"
done
