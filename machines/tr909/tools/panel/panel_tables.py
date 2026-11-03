#!/usr/bin/env python3
"""Print the element tables of docs/panel.md (Markdown) from docs/panel.json.

Usage: panel_tables.py docs/panel.json > tables.md"""
import json, sys

d = json.load(open(sys.argv[1]))
E = d['elements']


def f(v):
    return '%.2f' % v


def centre(e):
    if 'x' in e:
        return '%s, %s' % (f(e['x']), f(e['y']))
    b = e['bbox']
    return '%s, %s' % (f((b[0] + b[2]) / 2), f((b[1] + b[3]) / 2))


def size(e):
    b = e['bbox']
    return '%s × %s' % (f(b[2] - b[0]), f(b[3] - b[1]))


def table(head, rows):
    out = ['| ' + ' | '.join(head) + ' |', '|' + '|'.join('---' for _ in head) + '|']
    out += ['| ' + ' | '.join(str(c) for c in r) + ' |' for r in rows]
    return '\n'.join(out)


sections = []
# knobs
rows = []
for e in E:
    if e['type'] == 'knob':
        sc = e['scale']
        rows.append([e['id'], '`%s`' % e.get('param', ''), e['label'], centre(e), f(e['knob_d']), e['knob_style'],
                     '%d ticks, r %.1f–%.1f (long %.1f at %s)' % (len(sc['angles_deg']), sc['r_inner'], sc['r_outer'], sc['r_outer_long'],
                                                                '0, ±150°' if e['knob_style'] == 'small' else 'every 30°')])
sections.append('### Knobs\n\n' + table(['id', 'param', 'function', 'centre x, y', 'top Ø', 'type', 'printed scale'], rows))
# keys
rows = []
for e in E:
    if e['type'] == 'key':
        rows.append([e['id'], '`%s`' % e['param'], e['label'], centre(e), size(e),
                     '%s, %s' % (e['shape'], e['key_style']) + (' (lit)' if e.get('lit') else ''), e.get('led', '')])
sections.append('### Keys and buttons\n\nSize = panel cut-out for the large keys, visible cap for the small buttons; '
                'param = the tr909::Machine::Key.\n\n' +
                table(['id', 'param', 'function', 'centre x, y', 'w × h', 'shape, style', 'LED'], rows))
# leds + display
rows = []
for e in E:
    if e['type'] in ('led', 'display'):
        rows.append([e['id'], '`%s`' % e['led'] if 'led' in e else '', centre(e), size(e), e.get('shape', 'window'), e.get('in', '')])
sections.append('### LEDs and display\n\nled = the tr909::Machine::Led.\n\n' + table(['id', 'led', 'centre x, y', 'w × h', 'shape', 'in'], rows))
# fills / label boxes / outline boxes
rows = []
for e in E:
    if e['type'] in ('region', 'fill', 'label_box', 'outline_box'):
        b = e['bbox']
        rows.append([e['id'], e.get('label', ''), '[%s, %s, %s, %s]' % tuple(f(v) for v in b), e['colour'],
                     ('polygon ' + ' '.join('(%s,%s)' % (f(x), f(y)) for x, y in e['points'])) if 'points' in e else ''])
sections.append('### Bars, boxes and fills\n\nRectangles as [x0, y0, x1, y1].\n\n' + table(['id', 'label', 'rect', 'colour', 'shape'], rows))
# lines
rows = []
for e in E:
    if e['type'] == 'line':
        rows.append([e['id'], ' → '.join('(%s, %s)' % (f(x), f(y)) for x, y in e['points']), f(e['stroke']), e['colour']])
sections.append('### Keylines and white lines\n\n' + table(['id', 'points', 'width', 'colour'], rows))
# texts
rows = []
for e in E:
    if e['type'] == 'text':
        rows.append(['`%s`' % e['label'], e['id'], centre(e), f(e['bbox'][2] - e['bbox'][0]), f(e['cap_h']),
                     e['font'].replace(', caps', ''), e['colour']])
sections.append('### Every printed legend\n\nCentre and width of the ink box, cap height (mm).\n\n' +
                table(['text', 'id', 'centre x, y', 'width', 'cap', 'font', 'colour'], rows))
# notes
rows = []
for e in E:
    if e['type'] == 'symbol':
        rows.append([e['id'], e['label'].replace('_', ' '), '[%s, %s, %s, %s]' % tuple(f(v) for v in e['bbox'])])
sections.append('### Note glyphs in the scale bars\n\n' + table(['id', 'glyph', 'bbox'], rows))
# logos
rows = [['`%s`' % e['logo'], e['label'], '[%s, %s, %s, %s]' % tuple(f(v) for v in e['bbox']), e['colour']] for e in E if e['type'] == 'logo']
sections.append('### Brand-mark slots\n\nThe artwork pack\'s ids; slots only.\n\n' + table(['pack id', 'mark', 'bbox', 'colour'], rows))
print('\n\n'.join(sections))
