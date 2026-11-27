"""Shared paths, seeds and helpers for the MS-20 panel tools (docs/panel.md, section 1).

Frames:
  * photo: pixels of the master photo (Commons "Korg_Synthesizer_MS-20.jpg", 7875 x 5296).
  * drawing: pixels of the service manual's board front views (1994 TIFF scans, 300 dpi), KLM-127
    as printed (turned to landscape), KLM-128C as printed (portrait: the board is drawn turned
    90 deg, panel top on the drawing's left).
  * panel: mm on the front panel's face, seen straight-on. Origin = top-left corner of the unit's
    front outline (outer face of the left side panel, top of the side panel); x right, y down the
    panel face. The keyboard is unfolded below it (docs/panel.md section 2).

Seeds are rough photo positions (px) the measuring scripts start from; everything they report is
measured, not these numbers."""
import os
import json
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
MACHINE = os.path.abspath(os.path.join(HERE, '..', '..'))          # machines/ms20
ROOT = os.path.abspath(os.path.join(MACHINE, '..', '..'))          # the repo
PRIVATE = os.path.abspath(os.path.join(ROOT, '..', 'private', 'ms20'))
PHOTOS = os.path.join(PRIVATE, 'docs', 'photos')
OUT = os.path.join(PHOTOS, 'tools')                                # private outputs (photo derivatives)
TIF = os.path.join(PRIVATE, 'docs', 'manuals', 'service_hyperreal_1994_tif', 'tif')

MASTER = os.path.join(PHOTOS, 'Korg_Synthesizer_MS-20.jpg')
MASTER_SHA1 = '771ff23bae40583c13f993aa2e08d54882a1f265'
KLM127 = os.path.join(TIF, 'korg_ms20_6.tif')
KLM128 = os.path.join(TIF, 'korg_ms20_7.tif')

# Camera (EXIF of the master): Nikon Z 7II, 28.5 mm, pixel pitch 1/2301.32 cm = 4.345 um.
FOCAL_MM = 28.5
PIXEL_UM = 1e4 / 2301.3246154785156
F_PX = FOCAL_MM * 1000.0 / PIXEL_UM            # ~6559 px, if Photoshop did not resample

SPEC_W = 569.0          # service manual section 1, "Dimensions 569(W) x 308(D) x 249(H) mm"
PPM = 10.0              # px/mm of the rectified panel image

S = 7875 / 1600.0       # seed positions were read on a 1600 px wide preview


def sp(x, y):
    return (x * S, y * S)


# Knobs and rotary switches: id -> (seed x, y on the 1600 px preview, board, part on the board).
# board 127 = KLM-127 (its front view gives the shaft centre); None = not on a drawn board.
KNOBS = {
    'vco1_wave': (270, 265, '127', 'SW1'),
    'vco1_pw': (270, 350, '127', 'VR9'),
    'vco1_scale': (270, 428, '127', 'SW2'),
    'vco2_wave': (362, 265, '127', 'SW4'),
    'vco2_pitch': (362, 350, '127', 'VR12'),
    'vco2_scale': (362, 428, '127', 'SW3'),
    'portamento': (270, 518, '127', 'VR10'),
    'master_tune': (362, 518, '127', 'VR11'),
    'vco1_level': (440, 270, '127', 'VR13'),
    'vco2_level': (440, 355, '127', 'VR14'),
    'fm_mg': (440, 462, '127', 'VR15'),
    'fm_eg1': (440, 518, '127', 'VR16'),
    'hpf_cutoff': (518, 265, '127', 'VR20'),
    'hpf_peak': (518, 352, '127', 'VR19'),
    'hpf_mg': (518, 462, '127', 'VR18'),
    'hpf_eg2': (518, 518, '127', 'VR17'),
    'lpf_cutoff': (597, 265, '127', 'VR21'),
    'lpf_peak': (597, 352, '127', 'VR22'),
    'lpf_mg': (597, 462, '127', 'VR23'),
    'lpf_eg2': (597, 518, '127', 'VR24'),
    'mg_wave': (674, 462, '127', 'VR26'),
    'mg_freq': (674, 518, '127', 'VR25'),
    'eg1_delay': (748, 405, '127', 'VR27'),
    'eg1_attack': (748, 462, '127', 'VR28'),
    'eg1_release': (748, 518, '127', 'VR29'),
    'eg2_hold': (822, 270, '127', 'VR34'),
    'eg2_attack': (822, 345, '127', 'VR33'),
    'eg2_decay': (822, 405, '127', 'VR32'),
    'eg2_sustain': (822, 462, '127', 'VR31'),
    'eg2_release': (822, 518, '127', 'VR30'),
    'volume': (1222, 262, None, None),
    'esp_level': (899, 518, None, None),
    'esp_lowcut': (962, 518, None, None),
    'esp_highcut': (1022, 518, None, None),
    'esp_cvadj': (1080, 518, None, None),
    'esp_threshold': (1140, 518, None, None),
}
ROTARY = ('vco1_wave', 'vco1_scale', 'vco2_wave', 'vco2_scale')
LARGE = ('vco1_wave', 'vco1_scale', 'vco2_wave', 'vco2_scale', 'hpf_cutoff', 'lpf_cutoff', 'volume')

# Jacks: id -> (seed x, y in master photo px, J number on KLM-128C).
JACKS = {
    'totalext': (4391, 1377, 'J4'), 'extfreq': (4673, 1377, 'J6'), 'extsig': (4904, 1377, 'J13'),
    'hpfext': (5128, 1377, 'J14'), 'lpfext': (5374, 1377, 'J21'), 'initgain': (5627, 1377, 'J22'),
    'sigout': (6285, 1186, 'J35'), 'phones': (6285, 1377, 'J34'),
    'mg': (4394, 1635, 'J3'), 'mgsq': (4789, 1635, 'J7'), 'eg1out': (4977, 1635, 'J12'),
    'eg2rev': (5736, 1635, 'J23'), 'vco2cv': (5902, 1729, 'J24'), 'vcocv': (6090, 1635, 'J26'),
    'kbdcv': (6285, 1635, 'J33'),
    'shclk': (4591, 1830, 'J5'), 'eg1rev': (4977, 1830, 'J11'), 'mvcactl': (5175, 1830, 'J15'),
    'eg1trig': (5483, 1830, 'J20'), 'trig': (6090, 1830, 'J27'), 'trigout': (6285, 1830, 'J32'),
    'shin': (4394, 2025, 'J2'), 'shout': (4784, 2025, 'J8'), 'mvcain': (4977, 2025, 'J10'),
    'mvcaout': (5283, 2025, 'J16'), 'pink': (5478, 2025, 'J19'), 'white': (5869, 2025, 'J25'),
    'wheel': (6083, 2025, 'J28'), 'mom': (6282, 2025, 'J31'),
    'espin': (4391, 2298, 'J1'), 'espamp': (4781, 2298, 'J9'), 'espfilt': (5179, 2298, 'J17'),
    'espfv': (5565, 2298, 'J18'), 'espenv': (6083, 2298, 'J29'), 'esptrig': (6278, 2298, 'J30'),
}


def load_json(path):
    with open(path) as f:
        return json.load(f)


def save_json(obj, path):
    with open(path, 'w') as f:
        json.dump(obj, f, indent=1)


def r2(v, nd=2):
    if isinstance(v, (list, tuple, np.ndarray)):
        return [r2(t, nd) for t in v]
    return round(float(v), nd)


def hexc(rgb):
    return '#%02X%02X%02X' % tuple(int(round(max(0, min(255, c)))) for c in rgb)


def unit(deg):
    """Unit vector of a dial angle (deg clockwise from 12 o'clock) in image axes (y down)."""
    a = np.radians(deg)
    return np.array([np.sin(a), -np.cos(a)])


def angle_of(dx, dy):
    return float(np.degrees(np.arctan2(dx, -dy)))
