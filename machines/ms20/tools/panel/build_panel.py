#!/usr/bin/env python3
"""Write docs/panel.json from the measurements (docs/panel.md).

Reads the private outputs of the measuring scripts (photos/tools/*.json) and writes the
repo's panel.json: numbers only, no photo content. Brand marks are boxes.

Usage: build_panel.py OUT_PANEL_JSON"""
import sys, os
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ms20_common as C
import panel_tables as PT

O = C.OUT
W = C.SPEC_W


def J(name):
    return C.load_json(os.path.join(O, name))


def r2(v, nd=2):
    return C.r2(v, nd)


def box_el(eid, typ, bbox, **kw):
    x0, y0, x1, y1 = bbox
    e = dict(id=eid, type=typ, bbox=r2(bbox))
    e.update(kw)
    e.update(x=r2((x0 + x1) / 2), y=r2((y0 + y1) / 2), w=r2(x1 - x0), h=r2(y1 - y0))
    return e


def norm(e, H):
    if 'x' in e and 'y' in e:
        e['nx'] = round(e['x'] / W, 4)
        e['ny'] = round(e['y'] / H, 4)
    return e


# section of each knob (for grouping) and the legend element under it
SECTION = {
    'vco1_wave': 'vco1', 'vco1_pw': 'vco1', 'vco1_scale': 'vco1',
    'vco2_wave': 'vco2', 'vco2_pitch': 'vco2', 'vco2_scale': 'vco2',
    'portamento': 'portamento', 'master_tune': 'master_tune',
    'vco1_level': 'vco_mixer', 'vco2_level': 'vco_mixer',
    'fm_mg': 'frequency_modulation', 'fm_eg1': 'frequency_modulation',
    'hpf_cutoff': 'hpf', 'hpf_peak': 'hpf', 'hpf_mg': 'hpf_modulation', 'hpf_eg2': 'hpf_modulation',
    'lpf_cutoff': 'lpf', 'lpf_peak': 'lpf', 'lpf_mg': 'lpf_modulation', 'lpf_eg2': 'lpf_modulation',
    'mg_wave': 'mg', 'mg_freq': 'mg',
    'eg1_delay': 'eg1', 'eg1_attack': 'eg1', 'eg1_release': 'eg1',
    'eg2_hold': 'eg2', 'eg2_attack': 'eg2', 'eg2_decay': 'eg2', 'eg2_sustain': 'eg2', 'eg2_release': 'eg2',
    'volume': 'volume',
    'esp_level': 'esp', 'esp_lowcut': 'esp', 'esp_highcut': 'esp', 'esp_cvadj': 'esp', 'esp_threshold': 'esp',
}
LABEL = {k: 'l_' + k for k in SECTION}
LABEL.update(portamento='l_portamento', master_tune='t_master_tune')
NAMES = {
    'vco1_wave': 'VCO 1 WAVE FORM', 'vco1_pw': 'VCO 1 PW', 'vco1_scale': 'VCO 1 SCALE',
    'vco2_wave': 'VCO 2 WAVE FORM', 'vco2_pitch': 'VCO 2 PITCH', 'vco2_scale': 'VCO 2 SCALE',
    'portamento': 'PORTAMENTO TIME', 'master_tune': 'MASTER TUNE',
    'vco1_level': 'VCO 1 LEVEL', 'vco2_level': 'VCO 2 LEVEL',
    'fm_mg': 'FREQUENCY MODULATION MG/T.EXT', 'fm_eg1': 'FREQUENCY MODULATION EG1/EXT',
    'hpf_cutoff': 'HPF CUTOFF FREQUENCY', 'hpf_peak': 'HPF PEAK',
    'hpf_mg': 'HPF CUTOFF FREQUENCY MODULATION MG/T.EXT', 'hpf_eg2': 'HPF CUTOFF FREQUENCY MODULATION EG2/EXT',
    'lpf_cutoff': 'LPF CUTOFF FREQUENCY', 'lpf_peak': 'LPF PEAK',
    'lpf_mg': 'LPF CUTOFF FREQUENCY MODULATION MG/T.EXT', 'lpf_eg2': 'LPF CUTOFF FREQUENCY MODULATION EG2/EXT',
    'mg_wave': 'MG WAVE FORM', 'mg_freq': 'MG FREQUENCY',
    'eg1_delay': 'EG 1 DELAY TIME', 'eg1_attack': 'EG 1 ATTACK TIME', 'eg1_release': 'EG 1 RELEASE TIME',
    'eg2_hold': 'EG 2 HOLD TIME', 'eg2_attack': 'EG 2 ATTACK TIME', 'eg2_decay': 'EG 2 DECAY TIME',
    'eg2_sustain': 'EG 2 SUSTAIN LEVEL', 'eg2_release': 'EG 2 RELEASE TIME',
    'volume': 'VOLUME (POWER OFF at the counter-clockwise end)',
    'esp_level': 'ESP SIGNAL LEVEL', 'esp_lowcut': 'ESP LOW CUT FREQ', 'esp_highcut': 'ESP HIGH CUT FREQ',
    'esp_cvadj': 'ESP CV ADJUST', 'esp_threshold': 'ESP THRESHOLD LEVEL',
}
# rotary switches: detent legends in the order of the plugin's choices
ROTARY = {
    'vco1_wave': [('g_vco1_tri', 'triangle'), ('g_vco1_saw', 'sawtooth'), ('g_vco1_pulse', 'pulse (PW)'),
                  ('g_vco1_noise', 'white noise')],
    'vco2_wave': [('g_vco2_saw', 'sawtooth'), ('g_vco2_square', 'square'), ('g_vco2_pulse', 'narrow pulse'),
                  ('l_vco2_ring', 'RING')],
    'vco1_scale': [("32'", "32'"), ("16'", "16'"), ("8'", "8'"), ("4'", "4'")],
    'vco2_scale': [("16'", "16'"), ("8'", "8'"), ("4'", "4'"), ("2'", "2'")],
}
SCALE_SYMBOLS = {
    'vco1_pw': [('g_pw_wide', -135), ('g_pw_top', 0), ('g_pw_narrow', 135)],
    'mg_wave': [('g_mg_left', -135), ('g_mg_top', 0), ('g_mg_right', 135)],
}

# jacks: (interface id) -> (panel name, direction, normalled to / note, legend ids)
JACK_INFO = {
    'totalext': ('TOTAL (EXT MODULATION)', 'in', 'MG triangle', ['j_totalext', 'v_totalext']),
    'extfreq': ('VCO 1+2 EXT FREQ (FREQ)', 'in', 'EG 1', ['j_extfreq', 'v_extfreq']),
    'extsig': ('EXT SIGNAL IN', 'in', None, ['j_extsig', 'v_extsig']),
    'hpfext': ('HPF CUTOFF FREQ', 'in', 'EG 2', ['j_hpfext', 'v_hpfext']),
    'lpfext': ('LPF CUTOFF FREQ', 'in', 'EG 2', ['j_lpfext', 'v_lpfext']),
    'initgain': ('VCA INITIAL GAIN', 'in', None, ['j_initgain', 'v_initgain']),
    'sigout': ('SIGNAL OUT', 'out', None, ['j_sigout']),
    'phones': ('PHONES', 'out', None, ['j_phones']),
    'mg': ('MG OUT (triangle)', 'out', None, ['j_mg', 'g_mg_tri_out', 's_mg_tri_p', 's_mg_tri_m']),
    'mgsq': ('MG OUT (rectangle)', 'out', None, ['j_mgsq', 'g_mg_sq_out', 's_mg_sq_p', 's_mg_sq_m']),
    'eg1out': ('EG 1 OUT', 'out', None, ['j_eg1out', 'g_eg1out_env', 's_eg1out_p', 's_eg1out_m']),
    'eg2rev': ('EG 2 REV OUT', 'out', None, ['j_eg2rev', 'g_eg2rev_env', 's_eg2rev_0']),
    'vco2cv': ('VCO 2 CV IN', 'in', 'VCO 1+2 CV', ['j_vco2cv_1', 'j_vco2cv_2', 'a_vco2']),
    'vcocv': ('VCO 1+2 CV IN', 'in', 'KBD CV', ['j_vcocv_1', 'j_vcocv_2', 'a_vco1']),
    'kbdcv': ('KBD CV OUT', 'out', None, ['j_kbdcv_1', 'j_kbdcv_2']),
    'shclk': ('S&H CLOCK', 'in', None, ['j_shclk', 'gt_shclk', 'gnd_shclk']),
    'eg1rev': ('EG 1 REV OUT', 'out', None, ['j_eg1rev', 'g_eg1rev_env', 's_eg1rev_p', 's_eg1rev_m']),
    'mvcactl': ('MVCA CONTROL IN PUT', 'in', None, ['j_mvcactl_1', 'j_mvcactl_2', 'v_mvcactl']),
    'eg1trig': ('EG 1 TRIG IN', 'in', 'EG 1+2 TRIG', ['j_eg1trig_1', 'j_eg1trig_2', 'gt_eg1trig', 'gnd_eg1trig']),
    'trig': ('EG 1+2 TRIG IN (KBD TRIG IN)', 'in', 'KBD TRIG', ['j_trig', 'gt_trig', 'gnd_trig']),
    'trigout': ('KBD TRIG OUT', 'out', None, ['j_trigout', 'gt_trigout', 'gnd_trigout']),
    'shin': ('S&H IN', 'in', None, ['j_shin']),
    'shout': ('S&H OUT', 'out', None, ['j_shout']),
    'mvcain': ('MVCA IN', 'in', None, ['j_mvcain']),
    'mvcaout': ('MVCA OUT', 'out', None, ['j_mvcaout']),
    'pink': ('PINK NOISE', 'out', None, ['j_pink', 'g_pink']),
    'white': ('WHITE NOISE', 'out', None, ['j_white', 'g_white']),
    'wheel': ('CONTROL WHEEL (out)', 'out', None, ['g_wheel', 'v_wheel']),
    'mom': ('MOMENTARY SWITCH (out)', 'out', None, ['g_mom', 'gt_mom', 'gnd_mom']),
    'espin': ('ESP SIGNAL IN', 'in', None, ['j_espin']),
    'espamp': ('ESP AMP OUT', 'out', None, ['j_espamp']),
    'espfilt': ('ESP BAND PASS FILTER OUT', 'out', None, ['j_espfilt']),
    'espfv': ('ESP F-V CONVERTER CV OUT', 'out', None, ['j_espfv_1', 'j_espfv_2']),
    'espenv': ('ESP ENVELOPE FOLLOWER ENV OUT', 'out', None, ['j_espenv']),
    'esptrig': ('ESP TRIG OUT', 'out', None, ['j_esptrig', 'gt_esptrig', 'gnd_esptrig']),
}
PATCH_H_JACKS = {'totalext', 'extfreq', 'vcocv', 'vco2cv', 'extsig', 'hpfext', 'lpfext', 'initgain', 'trig',
                 'eg1trig', 'shin', 'shclk', 'mvcain', 'mvcactl', 'espin', 'mg', 'mgsq', 'eg1rev', 'eg2rev',
                 'kbdcv', 'trigout', 'pink', 'white', 'shout', 'mvcaout', 'wheel', 'mom', 'espamp', 'espfilt',
                 'espfv', 'espenv', 'esptrig', 'sigout'}

FONT_OF = {'title': 'panel_title', 'legend': 'panel_legend', 'box': 'panel_box', 'small': 'panel_small',
           'name': 'panel_name'}


def main():
    sc = J('scale.json'); fit = J('rectify_fit.json'); chk = J('check_pcb.json')
    kn = J('knobs.json'); jk = J('jacks.json'); pr = J('print.json'); hw = J('hw.json')
    ky = J('keys.json'); col = J('colours.json')
    els = []
    # ------------------------------------------------------------------ outline and structure
    plate = [hw['plate_left']['value'], hw['plate_top']['value'], hw['plate_right']['value'], hw['plate_bottom']['value']]
    sl = hw['side_left_inner']['value']; sr = hw['side_right_inner']['value']
    lip_y = 211.0
    key_front = float(np.mean([p[1] for p in ky['white_front']]))
    face_h = ky['white_face_h']
    n_pan = np.array(ky['plane_normal'])
    face_bottom = key_front + face_h * abs(n_pan[1])
    rail_bottom = 297.4
    H = rail_bottom
    # chamfered top corners of the cover (lines y = a x + b, y = 0 on the flat top) and the side
    # panels' tops
    ts = hw['top_silhouette']
    al, bl_ = ts['chamfer_left']; ar, br_ = ts['chamfer_right']
    stl = ts['side_top_left']; str_ = ts['side_top_right']
    poly = [[0, stl], [(stl - bl_) / al, stl], [(0 - bl_) / al, 0], [(0 - br_) / ar, 0], [(str_ - br_) / ar, str_], [W, str_],
            [W, H], [0, H]]
    els.append(dict(id='case_outline', type='case', label='MS-20 front outline (front view)',
                    polygon=r2(poly),
                    bbox=[0.0, 0.0, W, r2(H)], colour='case_black',
                    note='front view of the unit, 569 mm wide (service manual spec). y = 0 is the top cover\'s far edge as seen straight-on; its corners are chamfered (about %.0f deg), and the side panels\' tops show at y %.1f.' % (0.5 * (ts['chamfer_left_deg'] + ts['chamfer_right_deg']), 0.5 * (stl + str_)) + ' The bottom (y %.1f) is the front edge of the bottom rail under the keys, projected from the key plane (+-3 mm).' % H,
                    x=W / 2, y=r2(H / 2), w=W, h=r2(H)))
    els.append(dict(id='top_cover', type='region', label='top cover band above the panel (seen end-on)', colour='top_cover',
                    polygon=r2([poly[1], poly[2], poly[3], poly[4], [poly[4][0], plate[1]], [poly[1][0], plate[1]]]),
                    bbox=r2([poly[1][0], 0.0, poly[4][0], plate[1]]), x=r2(W / 2), y=r2(plate[1] / 2),
                    note='the steel top cover seen end-on above the panel, between the side panels, with chamfered top corners'))
    els.append(box_el('side_left', 'region', [0.0, r2(stl), r2(sl), lip_y], label='left side panel, front face',
                      colour='side_panel', note='black side panel (fine-textured finish), its front face as seen; the inner edge stands about 2.5 mm proud of the panel face. It continues down as the keyboard\'s left cheek.'))
    els.append(box_el('side_right', 'region', [r2(sr), r2(str_), W, lip_y], label='right side panel, front face',
                      colour='side_panel', note='as side_left'))
    els.append(box_el('panel_face', 'fill', plate, label='front panel (printed metal plate)', colour='panel_paint',
                      note='satin dark blue-grey paint with white screen print; edges measured on the photo (straight to %.2f mm rms)' % max(hw[k]['rms'] for k in ('plate_left', 'plate_right', 'plate_top'))))
    els.append(box_el('lower_lip', 'region', [plate[0], plate[3], plate[2], lip_y], label='lower lip of the panel section (bezel over the keys)',
                      colour='lip_black', note='the case\'s front lip between the panel and the keys; the keys go in under its lower edge (y %.1f)' % lip_y))
    # ------------------------------------------------------------------ print: legends
    leg = pr['legends']
    for lid, text, cx, cy, font, align in PT.LEGENDS:
        if lid not in leg:
            continue
        r = leg[lid]
        grp = ('titles' if lid.startswith('t_') else 'patch' if lid.split('_')[0] in ('j', 'v', 'a', 'b', 'gt') else
               'controls')
        e = box_el(lid, 'text', r['bbox'], label=text.replace('  ', ' '), text=text, cap_h=r2(r['cap_h']),
                   stroke=r2(r['stroke'] or 0.3), font=FONT_OF[font], colour='print_white', align=align, group=grp)
        if lid == 'l_synthesizer':
            e['note'] = 'the generic word under the KORG logotype, in the panel\'s grotesque (not a mark). Drawn as text; artwork that includes it replaces it (see logo_maker.lockup_bbox).'
        if lid == 'l_esp_peak':
            e['note'] = 'label under the ESP PEAK LED'
        els.append(e)
    for sid, text, *_ in PT.SMALL:
        if sid not in pr['small']:
            continue
        r = pr['small'][sid]
        els.append(box_el(sid, 'text', r['bbox'], label=text, text=text, cap_h=r2(r['cap_h']), stroke=r2(r['stroke'] or 0.28),
                          font='panel_small', colour='print_white', align='left', group='patch'))
    # ------------------------------------------------------------------ print: boxes, triangles, symbols
    for k, b in pr['boxes'].items():
        els.append(box_el(k, 'outline_box', b['bbox'], corner_r=0.0, stroke=r2(b['stroke']), colour='print_white', group='patch',
                          note='flow-diagram box; bbox = stroke centre line'))
    for k, t in pr['triangles'].items():
        p = np.array(t['points'])
        e = dict(id=k, type='polygon', points=r2(p), closed=True, stroke=r2(t['stroke']), colour='print_white', fill=None,
                 note='amplifier triangle pointing right (stroke centre line)')
        e.update(bbox=r2([p[:, 0].min(), p[:, 1].min(), p[:, 0].max(), p[:, 1].max()]), x=r2(p[:, 0].mean()), y=r2(p[:, 1].mean()))
        els.append(e)
    for k, s in pr['symbols'].items():
        e = box_el(k, 'symbol', s['bbox'], label=s['label'], polylines=s['polylines'], stroke=r2(s['stroke'] or 0.3),
                   colour='print_white', join='round')
        if k == 'g_mom':
            e['note'] = 'a push button seen from the side (a filled cap on a stem): draw the polylines with this stroke, or fill the bbox'
        els.append(e)
    # ------------------------------------------------------------------ print: lines and arrows
    hardware = []
    for k, v in hw['screws'].items():
        hardware.append((np.array(v['c']), v['d'] / 2 + 1.0))
    for k, v in hw['plugs'].items():
        hardware.append((np.array(v['c']), v['d'] / 2 + 1.0))
    for k, v in hw['leds'].items():
        hardware.append((np.array(v['c']), v['d'] / 2 + 0.8))
    af = float(np.median([j['nut_af'] for k, j in jk['jacks'].items() if k != 'sigout']))
    for k, j in jk['jacks'].items():
        hardware.append((np.array(j['c']), af / np.sqrt(3) + 0.9))
    logo_box = pr['logo_korg']['bbox']; syn = leg['l_synthesizer']['bbox']
    n_rule = n_flow = 0
    for i, l in enumerate(pr['lines']):
        p = np.array(l['points'])
        if l['length'] < 12:
            near = any(np.min(np.hypot(*(p - c).T)) < r for c, r in hardware)
            if near:
                continue
        inlogo = all((logo_box[0] - 1 <= x <= max(logo_box[2], syn[2]) + 9) and (logo_box[1] - 1 <= y <= syn[3] + 1) for x, y in p)
        if inlogo:
            continue
        kind = 'rule' if l['stroke'] >= 0.5 else 'flow'
        if kind == 'rule':
            n_rule += 1; lid = 'rule_%02d' % n_rule
        else:
            n_flow += 1; lid = 'flow_%03d' % n_flow
        e = dict(id=lid, type='line', kind=kind, points=r2(p), stroke=r2(l['stroke']), colour='print_white')
        e.update(bbox=r2([p[:, 0].min(), p[:, 1].min(), p[:, 0].max(), p[:, 1].max()]))
        els.append(e)
    for i, a in enumerate(pr['arrows']):
        p = np.array(a['points'])
        c = p.mean(0)
        if any(np.hypot(*(c - h)) < r - 0.5 for h, r in hardware):
            continue
        e = dict(id='arrow_%02d' % (i + 1), type='arrowhead', points=r2(p), colour='print_white',
                 note='filled triangle (the smallest triangle round the printed head; its sharpest corner is the tip)')
        e.update(bbox=r2([p[:, 0].min(), p[:, 1].min(), p[:, 0].max(), p[:, 1].max()]), x=r2(c[0]), y=r2(c[1]))
        els.append(e)
    # ------------------------------------------------------------------ knobs and rotary switches
    r_out_small = float(np.median([v['r_tick_out'] for v in kn.values() if not v['large']]))
    r_out_large = float(np.median([v['r_tick_out'] for v in kn.values() if v['large']]))
    tick_w = float(np.median([t['w'] for v in kn.values() for t in v['ticks'] if t['w']]))

    def far_r_in(v):
        best = None
        for t in v['ticks']:
            dd = abs(((t['deg'] - (v['foot_dir'] + 180)) + 180) % 360 - 180)
            if best is None or dd < best[0]:
                best = (dd, t['r_in'])
        return best[1] if best and best[0] < 40 else None
    rin_s = float(np.median([x for x in (far_r_in(v) for v in kn.values() if not v['large']) if x]))
    rin_l = float(np.median([x for x in (far_r_in(v) for v in kn.values() if v['large']) if x]))
    # numeral layouts: per scale type, the mean polar position of each numeral over the dials that
    # carry it; a numeral that sits more than 3 sd (min 0.6 mm) off its layout on one dial (merged
    # with a rule, or not found) is replaced there by the layout position
    def stype(k, v):
        sc_ = PT.KNOB_SCALE.get(k, 'n10')
        return sc_ + ('_large' if v['large'] and sc_ == 'n10' else '')
    lay = {}
    for k, v in kn.items():
        for nmr in v['numerals']:
            if nmr.get('found'):
                lay.setdefault(stype(k, v), {}).setdefault(nmr['text'], []).append((nmr['deg'], nmr['r'], nmr['cap_h']))
    layouts = {}
    for st, d in lay.items():
        layouts[st] = {}
        for txt, vals in d.items():
            a_ = np.array(vals)
            for it in range(2):
                m_ = np.median(a_, 0); s_ = np.maximum(a_.std(0), [1.0, 0.2, 0.1])
                keep = (np.abs(a_[:, 0] - m_[0]) < 3 * s_[0]) & (np.abs(a_[:, 1] - m_[1]) < 3 * s_[1])
                if keep.sum() >= 2:
                    a_ = a_[keep]
            layouts[st][txt] = dict(deg=round(float(a_[:, 0].mean()), 1), r=r2(float(a_[:, 1].mean())), cap_h=r2(float(a_[:, 2].mean())),
                                    sd_deg=round(float(a_[:, 0].std()), 1), sd_r=r2(float(a_[:, 1].std())), n=int(len(a_)))
    tick_dev = []
    for k, v in kn.items():
        large = v['large']
        c = v['c']
        rot = k in ROTARY
        design = [-45, -15, 15, 45] if rot else [-150 + 30 * i for i in range(11)]
        if k == 'volume':
            design = [-180] + [-150 + 30 * i for i in range(10)]
        meas = []
        for d in design:
            m = [t['deg'] for t in v['ticks'] if abs(((t['deg'] - d) + 180) % 360 - 180) < 5]
            if m:
                meas.append(m[0]); tick_dev.append((m[0] - d, v['r_tick_out']))
        scale = dict(ticks_deg=design, tick_r=r2([rin_l if large else rin_s, v['r_tick_out']]), tick_w=r2(tick_w),
                     ticks_measured=len(meas), font='panel_legend', colour='print_white')
        nums = []
        L_ = layouts.get(stype(k, v), {})
        for n in v['numerals']:
            ly = L_.get(n['text'])
            ok = n.get('found') and ly and abs(n['deg'] - ly['deg']) <= max(3 * ly['sd_deg'], 3.0) and abs(n['r'] - ly['r']) <= max(3 * ly['sd_r'], 0.6)
            if ok:
                nums.append(dict(text=n['text'], deg=round(n['deg'], 1), r=r2(n['r']), cap_h=r2(n['cap_h']),
                                 bbox=r2(n['bbox'])))
            elif ly:
                cx_ = c[0] + np.sin(np.radians(ly['deg'])) * ly['r']; cy_ = c[1] - np.cos(np.radians(ly['deg'])) * ly['r']
                nums.append(dict(text=n['text'], deg=ly['deg'], r=ly['r'], cap_h=ly['cap_h'], x=r2(cx_), y=r2(cy_),
                                 source='layout (on this dial the numeral touches a rule or was not found)'))
        if nums:
            scale['numerals'] = nums
            scale['numeral_cap_h'] = r2(float(np.median([n['cap_h'] for n in nums])))
        if k in SCALE_SYMBOLS:
            scale['symbols'] = [dict(id=s, deg=a) for s, a in SCALE_SYMBOLS[k]]
        d_knob = 23.6 if large else 17.4
        e = dict(id=k, type='rotary' if rot else 'knob', param=k, label=NAMES[k], section=SECTION[k],
                 x=r2(c[0]), y=r2(c[1]), d=d_knob, top_d=18.5 if large else 15.5, height=18.0 if large else 15.0,
                 knob='ms20_large' if large else 'ms20_small', legend=LABEL[k] if LABEL[k] in leg else None,
                 min_deg=-150, max_deg=150, scale=scale, colour='knob_black')
        if k == 'volume':
            e.update(min_deg=-180, max_deg=120, power_off_deg=-180,
                     note='VOLUME with the power switch: the scale runs 1 (-150 deg) to 10 (+120 deg); a further tick at the bottom (180 deg) is POWER OFF, the switch position at the counter-clockwise end (legend l_power_off under it).')
        if k == 'master_tune' or k == 'vco2_pitch':
            e['note'] = 'scale -5 ... 0 ... +5, 0 at 12 o\'clock'
        if rot:
            e.update(positions=4, detents_deg=[-45, -15, 15, 45], min_deg=-45, max_deg=45,
                     detent_legends=[dict(deg=d, legend=s, means=m) for d, (s, m) in zip([-45, -15, 15, 45], ROTARY[k])],
                     note='4-position rotary switch (KLM-127 %s) under the large knob; detents at the four printed ticks, in the order of the plugin\'s choices' % v['part'])
        if v['board']:
            e['source'] = 'centre: tick-ring centre in the master; checked against KLM-127 %s (residual %.2f mm)' % (
                v['part'], np.hypot(*chk['klm127']['residuals'][k]))
        else:
            e['source'] = 'centre: tick-ring centre in the master (not on a drawn board)'
        e['bbox'] = r2([c[0] - d_knob / 2, c[1] - d_knob / 2, c[0] + d_knob / 2, c[1] + d_knob / 2])
        e['w'] = e['h'] = d_knob
        els.append(e)
    # ------------------------------------------------------------------ jacks
    hole_d = float(np.median([j['hole_d'] for j in jk['jacks'].values()]))
    for k, j in jk['jacks'].items():
        name, io, normal, legs = JACK_INFO[k]
        c = j['c']
        e = dict(id=k, type='jack', label=name, j=j['j'], io=io, x=r2(c[0]), y=r2(c[1]),
                 d=r2(hole_d), nut_af=r2(af), nut_rot_deg=round(j['nut_rot_deg'], 1),
                 legends=[l for l in legs if l in leg or l in pr['small'] or l in pr['symbols']],
                 in_patch_h=k in PATCH_H_JACKS, colour='nut_chrome')
        if normal:
            e['normal'] = normal
        if k in chk['klm128']['residuals']:
            e['source'] = 'centre: the opening in the master (at its height, camera model); KLM-128C %s residual %.2f mm' % (
                j['j'], np.hypot(*chk['klm128']['residuals'][k]))
        else:
            e['source'] = 'centre: the opening in the master; J35 sits in a notch at the edge of KLM-128C (no drawn ring)'
        if k == 'eg1out':
            e['note'] = 'EG-1 OUT (J12): on the panel and in the circuit (docs/circuit eg1out) but not in core/patch.h'
        if k == 'phones':
            e['note'] = 'PHONES (J34): stereo phone jack, the headphone amp; not a patch point (not in core/patch.h)'
        e['bbox'] = r2([c[0] - af / np.sqrt(3), c[1] - af / np.sqrt(3), c[0] + af / np.sqrt(3), c[1] + af / np.sqrt(3)])
        els.append(e)
    # ------------------------------------------------------------------ LEDs, plugs, screws
    led_names = {'led_mg': ('MG rate', 'pulses at the MG frequency'), 'led_kbd': ('KBD trigger', 'lit while a key is down'),
                 'led_esp_peak': ('ESP PEAK', 'ESP input peak indicator; label l_esp_peak under it'),
                 'led_esp_trig': ('ESP TRIG', 'ESP trigger indicator, above TRIG OUT')}
    for k, v in hw['leds'].items():
        c = v['c']; d = 3.0
        nm, what = led_names[k]
        els.append(dict(id=k, type='led', label=nm, x=r2(c[0]), y=r2(c[1]), d=d, colour='led_off', colour_on='led_on',
                        bbox=r2([c[0] - d / 2, c[1] - d / 2, c[0] + d / 2, c[1] + d / 2]), w=d, h=d,
                        note='small red LED in a panel hole (looks dark when off in both colour photos); %s. Hole %.1f mm as measured.' % (what, v['d'])))
    mapped = chk['klm127']['mapped']
    for k, v in hw['plugs'].items():
        c = v['c']; d = 5.1
        vr = v['vr']
        res = np.hypot(*(np.array(mapped[vr]) - c)) if vr in mapped else None
        els.append(dict(id=k, type='hole', kind='plug', label='trimmer access plug (%s)' % vr, x=r2(c[0]), y=r2(c[1]), d=d,
                        bbox=r2([c[0] - d / 2, c[1] - d / 2, c[0] + d / 2, c[1] + d / 2]), w=d, h=d, colour='plug_black',
                        note='black plastic plug in a hole over trimmer %s on KLM-127 (drawn position %.1f mm away)' % (vr, res) if res is not None else ''))
    for k, v in hw['screws'].items():
        c = v['c']; d = 6.0
        els.append(dict(id=k, type='screw', x=r2(c[0]), y=r2(c[1]), d=d, bbox=r2([c[0] - 3, c[1] - 3, c[0] + 3, c[1] + 3]),
                        w=d, h=d, colour='screw_chrome', note='chrome pan-head Phillips screw holding the panel (head %.1f mm as measured)' % v['d']))
    # ------------------------------------------------------------------ logo
    lb = pr['logo_korg']['bbox']
    els.append(box_el('logo_maker', 'logo', lb, place='maker', label='KORG logotype', colour='logo_grey',
                      lockup_bbox=r2([min(lb[0], syn[0]), lb[1], max(lb[2], syn[2]), syn[3]]),
                      note='brand mark place, not drawn: the KORG logotype printed in light grey on the panel, bottom right. bbox = its ink box. lockup_bbox also covers the word SYNTHESIZER under it, for artwork that includes the word (then skip l_synthesizer).'))
    # ------------------------------------------------------------------ keyboard (orthographic front view of the key plane)
    pitch = ky['pitch']
    gx = np.array(ky['gap_x'])
    a = float(np.mean(gx - pitch * np.arange(1, 22)))       # left edge of white key 0
    gx_fit = a + pitch * np.arange(0, 23)
    gap_res = gx - gx_fit[1:22]
    d_key = np.array(ky['key_dir'])
    # black keys: their measured centres relative to the white grid
    bl = ky['blacks']
    off = {}
    for b in bl:
        gi = b['gap']
        cx = 0.5 * (b['left'][0] + b['right'][0])
        o = (cx - gx_fit[gi + 1]) / pitch            # offset from the gap they sit over, in pitches
        off.setdefault(gi % 7, []).append(o)
    boff = {gi: float(np.median(v)) for gi, v in off.items()}
    black_w = ky['black_width']
    black_front = float(np.median([b['front'][1] for b in bl]))
    keys = []
    white_semis = [0, 2, 4, 5, 7, 9, 11]
    names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
    gap_gi = {1: 0, 3: 1, 6: 3, 8: 4, 10: 5}            # black semitone -> gap index in the octave
    wi = 0
    for key in range(37):
        o, s = divmod(key, 12)
        nm = '%s%d' % (names[s], o + 3)  # MIDI C3-C6: 8' at the MIDI pitch
        if s in white_semis:
            x0 = gx_fit[wi]; x1 = gx_fit[wi + 1]
            e = dict(id='key_%d' % key, type='key', label=nm, key=key, white=True,
                     bbox=r2([x0 + 0.5, lip_y, x1 - 0.5, key_front]), front_face=r2([x0 + 0.5, key_front, x1 - 0.5, face_bottom]),
                     colour='key_white', x=r2((x0 + x1) / 2), y=r2((lip_y + key_front) / 2), w=r2(x1 - x0 - 1.0), h=r2(key_front - lip_y))
            wi += 1
        else:
            gi = o * 7 + gap_gi[s]
            cx = gx_fit[gi + 1] + boff.get(gap_gi[s], 0.0) * pitch
            e = dict(id='key_%d' % key, type='key', label=nm, key=key, white=False,
                     bbox=r2([cx - black_w / 2, lip_y, cx + black_w / 2, black_front]),
                     front_face=r2([cx - black_w / 2, black_front - 9.6, cx + black_w / 2, black_front]),
                     colour='key_black', x=r2(cx), y=r2((lip_y + black_front) / 2), w=r2(black_w), h=r2(black_front - lip_y))
        keys.append(e)
    els.extend(keys)
    # left keyboard cheek: wheel and momentary switch (mapped onto the key plane, +-5 mm)
    ch = ky['cheek']
    wl = [ch['wheel_back_left'], ch['wheel_back_right'], ch['wheel_front_right'], ch['wheel_front_left']]
    wp = np.array([[p[0], p[1]] for p in wl])
    els.append(box_el('cheek_left', 'region', [0.0, lip_y, r2(a - 0.5), H], label='left keyboard cheek (wheel and switch)', colour='cheek_black',
                      note='the left side panel round the keyboard; its top carries the control wheel and the momentary switch. Seen at an angle in the master: its shape here is approximate (+-5 mm).'))
    els.append(box_el('cheek_right', 'region', [r2(gx_fit[-1] + 0.5), lip_y, W, H], label='right keyboard cheek', colour='cheek_black'))
    els.append(box_el('rail_front', 'region', [r2(a - 0.5), r2(face_bottom), r2(gx_fit[-1] + 0.5), H], label='bottom rail in front of the keys',
                      colour='case_black'))
    e = dict(id='control_wheel', type='wheel', label='CONTROL WHEEL', param=None, opening=dict(polygon=r2(wp), colour='slot_black'),
             face=dict(w=r2(np.hypot(*(wp[1] - wp[0]))), colour='wheel_black', rib='moulded cross ribs about 2.5 mm apart'),
             orientation='front-back (along the keys)', rest='centre click (spec: "center click"), -5 V ... 0 ... +5 V at its jack',
             colour='wheel_black', note='on the left cheek, left of the keys; drives the CONTROL WHEEL jack (J28) and, through the plugin, pitch bend. Position from hand-picked points in the master mapped onto the key plane (+-5 mm).')
    e.update(bbox=r2([wp[:, 0].min(), wp[:, 1].min(), wp[:, 0].max(), wp[:, 1].max()]), x=r2(wp[:, 0].mean()), y=r2(wp[:, 1].mean()))
    els.append(e)
    mc = ch['mom_centre']; md = abs(ch['mom_ring_right'][0] - ch['mom_ring_left'][0])
    els.append(dict(id='momentary_switch', type='switch', kind='momentary', label='MOMENTARY SWITCH', x=r2(mc[0]), y=r2(mc[1]), d=r2(md),
                    bbox=r2([mc[0] - md / 2, mc[1] - md / 2, mc[0] + md / 2, mc[1] + md / 2]), w=r2(md), h=r2(md),
                    colour='button_black', ring='chrome ring',
                    note='black push button in a chrome ring, in front of the wheel; drives the MOMENTARY SWITCH jack (J31) (S-trigger to ground while held). +-5 mm.'))
    els = [norm(e, H) for e in els]
    # ------------------------------------------------------------------ accuracy summary numbers
    td = np.array(tick_dev)
    tick_rms_deg = float(np.sqrt(np.mean(td[:, 0] ** 2)))
    tick_rms_mm = float(np.sqrt(np.mean((np.radians(td[:, 0]) * td[:, 1]) ** 2)))
    acc = dict(
        rectification_line_rms_mm=r2(fit['fits']['homography+k1']['line_rms_px'] / sc['s'], 3),
        tick_ring_rms_mm=r2(fit['fits']['homography+k1']['tick_rms_px'] / sc['s'], 3),
        knobs_vs_klm127_rms_mm=r2(chk['klm127']['sim_rms'], 2), knobs_vs_klm127_max_mm=r2(chk['klm127']['sim_max'], 2),
        jacks_vs_klm128_rms_mm=r2(chk['klm128']['sim_rms'], 2), jacks_vs_klm128_max_mm=r2(chk['klm128']['sim_max'], 2),
        tick_angle_rms_deg=round(tick_rms_deg, 2), tick_angle_rms_mm=r2(tick_rms_mm, 2),
        key_gap_rms_mm=r2(float(np.sqrt(np.mean(gap_res ** 2))), 2),
    )
    colours = {
        'case_black': dict(hex='#141416', note='silhouette, bottom rail, under-key shadow'),
        'top_cover': dict(hex='#1E1F23', note='steel top cover seen end-on above the panel (master grey 33 against the panel\'s 46-96)'),
        'side_panel': dict(hex='#26272C', note='black side panels, fine texture; SMEM %s' % col['side_panel'][0]['hex']),
        'panel_paint': dict(hex='#30333B', note='satin dark blue-grey panel paint; SMEM daylight %s / %s / %s. The master is lit from above: grey 96 at the top to 46 at the bottom; draw it flat.' % tuple(v['hex'] for v in col['panel_paint'][:3])),
        'lip_black': dict(hex='#202126', note='lower lip over the keys'),
        'print_white': dict(hex='#EEF0F2', note='screen print, slightly cool white; SMEM highlights %s' % col['print_white'][0]['hex']),
        'logo_grey': dict(hex='#B8BBC2', note='the KORG logotype is printed light grey, not white (SMEM %s); used only for a blank logo place' % col['logo_grey'][0]['hex']),
        'knob_black': dict(hex='#1A1A1F', note='black plastic knobs, satin; SMEM %s' % col['knob_black'][0]['hex']),
        'pointer_white': dict(hex='#E8E8E6', note='white pointer line on each knob top, from the centre to the edge'),
        'nut_chrome': dict(hex='#B9BCC2', note='chrome hex nuts and bushings of the jacks; highlights %s, mid %s' % (col['jack_chrome'][0]['hex'], col['jack_chrome'][1]['hex'])),
        'hole_black': dict(hex='#050505', note='jack openings'),
        'plug_black': dict(hex='#18181A', note='trimmer access plugs, black plastic, slightly domed'),
        'screw_chrome': dict(hex='#8A8A8E', note='chrome panel screws (pan-head Phillips)'),
        'led_off': dict(hex='#2A0D0E', note='red LEDs, unlit; they read near black in both colour photos'),
        'led_on': dict(hex='#FF2A1E', note='lit; add a soft glow'),
        'key_white': dict(hex='#F0ECE2', note='white keys, slightly warm (SMEM %s)' % col['key_white'][0]['hex']),
        'key_black': dict(hex='#0F1011', note='black keys, glossy (SMEM %s)' % col['key_black'][0]['hex']),
        'cheek_black': dict(hex='#121212', note='keyboard cheeks and the key well'),
        'wheel_black': dict(hex='#2C2B2A', note='control wheel, black with grey rib highlights (SMEM %s)' % col['wheel_black'][0]['hex']),
        'button_black': dict(hex='#151515', note='momentary switch cap'),
        'slot_black': dict(hex='#080808', note='wheel opening'),
    }
    fonts = {
        'panel_title': 'Section titles: a Helvetica-style grotesque, condensed (closest free: Liberation Sans Narrow Regular, metric-compatible with Arial Narrow), white caps, cap height %.2f mm, stroke %.2f mm (FWHM, %d %% of the cap height), letter-spacing about 0.36 mm.' % (
            np.median([l['cap_h'] for l in leg.values() if l['font'] == 'title']), np.median([l['stroke'] for l in leg.values() if l['font'] == 'title' and l['stroke']]),
            round(100 * np.median([l['stroke'] for l in leg.values() if l['font'] == 'title' and l['stroke']]) / np.median([l['cap_h'] for l in leg.values() if l['font'] == 'title']))),
        'panel_legend': 'Control labels, dial numerals, jack names and voltage ranges: the same face, cap height %.2f mm, stroke %.2f mm.' % (
            np.median([l['cap_h'] for l in leg.values() if l['font'] == 'legend']), np.median([l['stroke'] for l in leg.values() if l['font'] == 'legend' and l['stroke']])),
        'panel_box': 'Text inside the patch panel\'s boxes: the same face, cap height %.2f mm, left-aligned 1.6-2.2 mm in from the box line.' % np.median([l['cap_h'] for l in leg.values() if l['font'] == 'box']),
        'panel_small': 'The small two-line level marks (+5V/0V, +2.5V) and GND: the same face, cap height %.2f mm.' % np.median([v['cap_h'] for v in pr['small'].values()]),
        'panel_name': 'SYNTHESIZER under the logo: the same grotesque, not condensed as much, cap height %.2f mm, stroke %.2f mm.' % (leg['l_synthesizer']['cap_h'], leg['l_synthesizer']['stroke'] or 0),
        'fit': 'font_fit.py: over 69 one-word legends, Arial Narrow (= Liberation Sans Narrow) predicts the ink widths to 0.37 mm rms with x-scale 0.78 and 0.36 mm tracking, or 0.46 mm rms at x-scale 0.95 with no tracking; Helvetica/Arial need x-scale 0.65. Fit every legend to its measured bbox width.',
        'substitute': 'Not in the repo: add Liberation Sans Narrow (SIL OFL 1.1) or Roboto Condensed (Apache 2.0). Already in the repo: IBM Plex Mono and JetBrains Mono (third_party/fonts), Michroma (machines/virusc/fonts): none is close.',
        'logos': 'The KORG logotype is not drawn: its place (logo_maker, place maker) is filled from the user\'s artwork pack.',
    }
    conventions = {
        'text': 'bbox = ink box (cap top to baseline, sub-pixel half-maximum edges); x,y = bbox centre; cap_h = capital height; stroke = FWHM of the vertical stems; align = how the legend sits on its control or box; text keeps double spaces where the print has a wide word gap',
        'outline_box': 'bbox = stroke centre line of a square-cornered box',
        'polygon': 'points = stroke centre line, closed',
        'line': 'points = stroke centre polyline (Douglas-Peucker to 0.12 mm); kind rule = section rules and frames (0.6-1.1 mm), flow = the patch panel\'s signal lines (0.25-0.3 mm)',
        'arrowhead': 'points = a filled triangle',
        'symbol': 'polylines = stroke centre lines of a printed glyph (wave shapes, ground marks); stroke = width; round joins',
        'knob': 'x,y = shaft centre (the printed tick ring\'s centre); d = knob base (skirt) diameter, top_d = top, height (estimates); angles in degrees clockwise from 12 o\'clock; scale.tick_r = [inner, outer] radius of the printed ticks; numerals at radius r to the numeral\'s ink-box centre; pointer from min_deg to max_deg',
        'rotary': 'as knob, with detents_deg and detent_legends (the printed symbol or numeral at each detent, in the plugin\'s choice order)',
        'jack': 'x,y = centre of the opening; d = opening; nut_af = hex nut across flats; j = J number (service manual); io = in/out; legends = the print that belongs to it; in_patch_h = in core/patch.h',
        'key': 'front view (orthographic projection onto the panel plane) of the key plane: bbox = visible top from the lip to the front edge; front_face = the front face below it; key 0 = C3 ... key 36 = C6 (plugin: MIDI 48-84; the unit's C-1 .. C-4); black keys on top of white keys',
        'logo': 'artwork place: a box the hardware view leaves for the user\'s artwork file (place name in "place")',
        'hole': 'trimmer access plug: a black plug in a panel hole',
    }
    panel = dict(w=W, h=r2(H), official_dimensions_mm=dict(w=569, d=308, h=249, weight_kg=7.7,
                 note='service manual section 1, item 15-16 ("569(W) x 308(D) x 249(H) mm", "7.7 kgs")'),
                 aspect=round(W / H, 4), panel_face=r2(plate), lip_y=lip_y,
                 key_plane=dict(tilt_to_panel_deg=round(ky['tilt_deg'], 1), white_pitch=r2(pitch, 3), white_visible_len=r2(ky['white_len'], 1),
                                black_visible_len=r2(ky['black_len'], 1), white_face_h=r2(face_h, 1), black_rise=11.0,
                                note='the white keys\' top surface is a plane at %.1f deg to the panel face (the panel leans back); keys are drawn in orthographic projection onto the panel plane, so their lengths are shortened by %.3f. black_rise (black key top above the white key top) is a standard-keyboard estimate.' % (ky['tilt_deg'], abs(d_key[1]))))
    out = dict(
        machine='ms20',
        unit='Korg MS-20 (1978), Rev 1 (two KORG35 filters; KLM-127, KLM-128C, KLM-129); front panel as made 1978-83',
        source_image='private/ms20/docs/photos/Korg_Synthesizer_MS-20.jpg (CC0, Wilfredor, 7875 x 5296, SHA1 %s)' % C.MASTER_SHA1,
        source_note='Front view from the Commons studio photo (Nikon Z 7II, 28.5 mm), rectified with a lens-distortion + homography model fitted to the panel\'s own straight print and round tick rings, scaled by the service manual\'s 569 mm overall width (side panels\' outer faces, parallax removed with the camera model). Independent check: the service manual\'s board front views (KLM-127 knob shafts, KLM-128C jacks) fit the result to %.2f / %.2f mm rms by a similarity, and their scales agree with each other and the width (panel.md section 1). Colours from the colour photos (Korg_MS-20.jpg, Zinnmann, CC BY-SA 3.0; SMEM Playroom, CC BY 4.0). Photos are reference only (private/ms20/docs/photos/SOURCES.md).' % (chk['klm127']['sim_rms'], chk['klm128']['sim_rms']),
        units='mm',
        origin='top-left corner of the front view: x = 0 on the left side panel\'s outer face, y = 0 on the top cover\'s far edge as seen straight-on; x right, y down the panel face (down on screen); the keyboard below it in orthographic projection',
        panel=panel,
        accuracy='knob centres +-0.3 mm, jack centres +-0.25 mm (rms vs the service manual\'s boards %.2f / %.2f mm); printed legends, boxes, lines, ticks, numerals +-0.2 mm relative, +-0.4 mm absolute; scale +-0.4 %% (569 mm spec, side-panel height +-2.5 mm); plate and lip edges +-0.3 mm; screws, plugs, LEDs +-0.5 mm; keyboard x +-1.5 mm, key lengths +-3 mm (standard 6.5 in octave assumed); left cheek, wheel and switch +-5 mm; knob heights and top diameters are estimates +-2 mm' % (chk['klm127']['sim_rms'], chk['klm128']['sim_rms']),
        accuracy_numbers=acc,
        colours=colours, fonts=fonts, conventions=conventions,
        numeral_layouts=dict(note='mean polar position (deg clockwise from 12 o\'clock, r to the ink-box centre, mm) of each dial numeral over all dials with that scale; sd and n say how regular the print is', **layouts),
        elements=els,
        ui_suggestions=[
            dict(id='ui_layout', type='note', note='Full front view %.0f x %.0f mm: at 6.8 px/mm 3869 x %.0f px; at 2.3 px/mm 1309 x %.0f px (the 2.2 mm legends become 5 px). The panel alone (y 0-211) at 2.3 px/mm is 1309 x 485 px.' % (W, H, H * 6.8, H * 2.3)),
            dict(id='ui_patch', type='note', note='Cables: 33 of the 35 jacks are patch points (in core/patch.h); PHONES (J34) is the headphone output and EG 1 OUT (J12) is missing from patch.h (see docs/panel.md section 7).'),
        ],
    )
    C.save_json(out, sys.argv[1])
    print('wrote %s: %d elements' % (sys.argv[1], len(els)))
    from collections import Counter
    print(Counter(e['type'] for e in els))
    print(acc)


if __name__ == '__main__':
    main()
