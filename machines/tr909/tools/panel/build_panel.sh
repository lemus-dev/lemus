#!/bin/sh
# Rebuild the TR-909 panel description from the photos (docs/panel.md, "Method"):
#   docs/panel.json, and in build/tr909-panel/panel/ the rectified and regularised master, what
#   measure_panel.py measured (measured.json), the cross-check (crosscheck/: crosscheck.json, the
#   other photos resampled onto the master) and the overlays.
# The pictures it writes show the photos (copyrighted): they stay in the build folder.
# Needs the repo's .venv (numpy, scipy, opencv-python-headless, pillow) and the private folder
# (machines/tr909/private: the photos and the service notes' page images).
set -e
export PYTHONDONTWRITEBYTECODE=1
M=$(cd "$(dirname "$0")/../.." && pwd)        # machines/tr909
ROOT=$(cd "$M/../.." && pwd)                  # the repo
PY="$ROOT/.venv/bin/python -I"
T="$M/tools/panel"
B="$ROOT/build/tr909-panel/panel"
PH="$M/private/docs/photos"
mkdir -p "$B"
MASTER="$PH/soundgas/sn282297_Roland-TR-909-Rhythm-Composer-SN282297-Ed-Obrien-2.jpg"

$PY "$T/rectify.py" "$MASTER" "$B/master_rect.png" "$B/master_rect.json" --ppm 8
$PY "$T/regularise.py" "$B/master_rect.png" "$B/master_rect.json" "$B/master_reg.png" "$B/master_reg.json"
$PY "$T/measure_panel.py" "$B/master_reg.png" "$B/master_rect.json" "$B/master_reg.json" "$B/measured.json" "$B/overlay_measured.jpg"
$PY "$T/crosscheck.py" "$B/master_rect.png" "$B/master_reg.png" "$B/measured.json" "$B/crosscheck" \
    --pots "$M/private/docs/schematic/p10-voicing-board-pcb-address-map.png" \
    "$PH/commons_tr909_front_909bd.jpg:909bd" \
    "$PH/soundgas/dm_Roland-TR-909-Rhythm-Composer-Drum-Machine-3.jpg:dm3" \
    "$PH/commons_tr909_large_bdaniel.jpg:bdaniel"
$PY "$T/lemus_panel.py" "$B/measured.json" "$M/docs/panel.json" --crosscheck "$B/crosscheck/crosscheck.json"
$PY "$T/render_preview.py" "$M/docs/panel.json" "$B/master_reg.png" "$B/preview.png" --ppmm 4 \
    --overlay "$B/master_reg.png" "$B/overlay_master.jpg" \
    --overlay "$B/crosscheck/reg_909bd.png" "$B/overlay_909bd.jpg" \
    --overlay "$B/crosscheck/reg_dm3.png" "$B/overlay_dm3.jpg" \
    --overlay "$B/crosscheck/reg_bdaniel.png" "$B/overlay_bdaniel.jpg"
echo done
