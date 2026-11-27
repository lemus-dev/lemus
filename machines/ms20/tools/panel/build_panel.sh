#!/bin/sh
# Rebuild the MS-20 front panel description from the photos (docs/panel.md, section 1):
#   docs/panel.json, and in the private photos/tools/ folder every intermediate measurement, the
#   rectified master (master_rect.png) and the overlay check (panel_overlay_check.jpg).
# The pictures it writes show the photo (CC0 master, but kept with the other photos): they stay in
# the private folder. Needs the repo's .venv (numpy, scipy, opencv, pillow, scikit-image,
# fontTools) and the private folder (machines/ms20/private: docs/photos, docs/manuals).
set -e
export PYTHONDONTWRITEBYTECODE=1
M=$(cd "$(dirname "$0")/../.." && pwd)        # machines/ms20
ROOT=$(cd "$M/../.." && pwd)                  # the repo
PY="$ROOT/.venv/bin/python -I"
T="$M/tools/panel"
O=$(cd "$ROOT/../private/ms20/docs/photos" && pwd)/tools
mkdir -p "$O"

$PY "$T/photo_centres.py" "$O/photo_centres.json"          # dial tick rings, jack openings (photo px)
$PY "$T/pcb_refs.py" "$O/pcb_refs.json"                    # KLM-127 shafts, KLM-128C jacks (drawing px)
$PY "$T/photo_lines.py" "$O/photo_lines.json"              # long straight print (photo px)
$PY "$T/rectify_fit.py" "$O/photo_centres.json" "$O/photo_lines.json" "$O/rectify_fit.json"
$PY "$T/scale.py" "$O/rectify_fit.json" "$O/scale.json"    # 569 mm width, origin
$PY "$T/check_pcb.py" "$O/photo_centres.json" "$O/pcb_refs.json" "$O/check_pcb.json"
$PY "$T/rectify_image.py" "$O/master_rect.png" -10 -5 580 450
$PY "$T/measure_knobs.py" "$O/knobs.json"
$PY "$T/measure_jacks.py" "$O/jacks.json"
$PY "$T/measure_hw.py" "$O/hw.json"
$PY "$T/measure_print.py" "$O/print.json"
$PY "$T/measure_keys.py" "$O/keys.json"
$PY "$T/colours.py" "$O/colours.json"
$PY "$T/font_fit.py" "$O/print.json"
$PY "$T/build_panel.py" "$M/docs/panel.json"
$PY "$T/overlay.py" "$M/docs/panel.json" "$O/panel_overlay_check.jpg"
echo done
