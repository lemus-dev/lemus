#!/usr/bin/env python3
"""The standalone apps' icon: the Lemus mark (four bars, a level meter) on the ground colour, with
the model under it for a machine's own app ("303"), or the mark alone for the Lemus hosts. Drawn in
the macOS icon grid (an 824 px rounded square in a 1024 px canvas); JUCE turns the PNG into the
.icns and .ico.

    tools/icon/make_icon.py 303 machines/tb303/icon.png
    tools/icon/make_icon.py "" host/icon.png

Renders with headless Google Chrome."""
import html
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'

# Colours from host/Style.h: ground, accent, title, rule.
PAGE = '''<!doctype html><meta charset="utf-8"><style>
@font-face {{ font-family: "JetBrains Mono"; src: url("{font}"); font-weight: 700; }}
html, body {{ margin: 0; width: 1024px; height: 1024px; background: transparent; }}
svg {{ display: block; }}
</style>
<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 1024 1024">
  <defs><filter id="s" x="-10%" y="-10%" width="120%" height="130%"><feDropShadow dx="0" dy="10" stdDeviation="12" flood-color="#000" flood-opacity="0.35"/></filter></defs>
  <rect x="100" y="100" width="824" height="824" rx="185" fill="#1a1b26" filter="url(#s)"/>
  <rect x="101.5" y="101.5" width="821" height="821" rx="183.5" fill="none" stroke="#565f89" stroke-opacity="0.45" stroke-width="3"/>
  <g stroke="#7aa2f7" stroke-width="{bar}" stroke-linecap="round" fill="none">{bars}</g>
  {label}
</svg>'''


def render(label: str, out: pathlib.Path) -> None:
    # The mark's bars, 4 : 10 : 16 : 8 high, bottoms aligned; higher with no label under them.
    bottom, unit, bar = (600, 24, 56) if label else (752, 30, 70)
    pitch = unit * 5
    xs = [512 + (i - 1.5) * pitch for i in range(4)]
    bars = ''.join(f'<path d="M{x:.0f} {bottom - h * unit + bar / 2:.0f}V{bottom - bar / 2:.0f}"/>' for x, h in zip(xs, (4, 10, 16, 8)))
    text = (f'<text x="512" y="790" text-anchor="middle" font-family="JetBrains Mono" font-weight="700" font-size="150" '
            f'letter-spacing="-3" fill="#a9c1ff">{html.escape(label)}</text>') if label else ''
    font = (ROOT / 'third_party/fonts/JetBrainsMono-Bold.ttf').as_uri()
    with tempfile.TemporaryDirectory() as tmp:
        page = pathlib.Path(tmp) / 'icon.html'
        page.write_text(PAGE.format(font=font, bar=bar, bars=bars, label=text))
        subprocess.run([CHROME, '--headless=new', f'--user-data-dir={tmp}/profile', '--disable-gpu', '--hide-scrollbars',
                        '--default-background-color=00000000', '--force-device-scale-factor=1', '--window-size=1024,1024',
                        '--virtual-time-budget=2000', f'--screenshot={out.resolve()}', page.as_uri()],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    render(sys.argv[1], pathlib.Path(sys.argv[2]))
