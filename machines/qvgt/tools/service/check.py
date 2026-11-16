#!/usr/bin/env python3
"""The service manual's checks. The GT's has no adjustments or measurements; the QuadraVerb's (5.0
Test Procedures, which the GT's firmware keeps) has the self-diagnostics: MIDI out looped to MIDI
in, power up holding BYPASS and CONFIG; the unit tests its RAM, EPROM, button LEDs, MIDI and clip
LED, and stops with "ERROR IN ..." on a failure.

    check.py [--qvgtrun PATH] [--rom PATH]

Runs it through qvgtrun and checks the screens in order, the LEDs the button test lights, and
whether the DSP overflows during the clip LED test.
"""
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "../../../.."))

SCREENS = ["ALL RAM OK", "EPROM OK", "LED TEST", "MIDI IN/OUT OK", "CLIP LED TEST", "QUADRAVERB  GT"]
# Known gaps (docs/improvements.md): checks that fail for a reason written down there.
KNOWN = {}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--qvgtrun", default=os.path.join(REPO, "build/qvgt/qvgtrun"))
    ap.add_argument("--rom", default=os.path.join(REPO, "../private/roms/qvgt/qvgtv103.bin"))
    args = ap.parse_args()
    r = subprocess.run([args.qvgtrun, args.rom, "-s", "26", "-l", "-k", "0:5:4", "-k", "0:14:4"],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stderr)
        return 1
    lcd, leds, overflow = [], set(), []
    test = None
    for line in r.stdout.splitlines():
        m = re.match(r"\[\s*([\d.]+) ms\] (.*)", line)
        if not m:
            continue
        t, what = float(m.group(1)), m.group(2)
        lm = re.match(r"LCD \|(.*)\|(.*)\|", what)
        if lm:
            lcd.append((t, lm.group(1) + " " + lm.group(2)))
            test = lm.group(2).strip()
        elif what.startswith("latch 0800") and test == "LED TEST":
            leds.add(what.split("=")[1].strip())
        elif what == "DSP overflow":
            overflow.append(test)
    screens = [s for k, (t, s) in enumerate(lcd) if (lcd[k + 1][0] if k + 1 < len(lcd) else 1e9) - t > 100]
    results = []
    at = 0
    for want in SCREENS:
        found = next((k for k in range(at, len(screens)) if want in screens[k]), None)
        results.append((f'"{want}"', found is not None))
        if found is not None:
            at = found + 1
    results.append(("no ERROR screen", not any("ERROR" in s for s in screens)))
    results.append(("the LED test walks the module LEDs", len(leds) >= 4))
    results.append(("the clip LED test overflows the DSP", "CLIP LED TEST" in overflow))
    failed = 0
    for what, ok in results:
        note = ""
        if not ok and what in KNOWN:
            note = f"  (known: {KNOWN[what]})"
        elif not ok:
            failed += 1
        print(f"  {'ok  ' if ok else 'FAIL'}  {what}{note}")
    print("\nall passed" if not failed else f"\n{failed} FAILED")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
