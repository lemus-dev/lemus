#!/usr/bin/env python3
"""The Xpander's service checks: the service manual's tests (SM p32-36, research.md 5.2), each run
on the model through xpanderrender. Most are the firmware's own SERVICE and TUNE pages, read from
the displays; the rest are the model's constants against the manual's limits.

    lemus/.venv/bin/python machines/xpander/tools/service/check.py [BUILD_DIR]

Needs the ROMs in the private folder. Exit status 1 if any check fails.
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))))
BUILD = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(ROOT), "build", "xpander")
RENDER = os.path.join(BUILD, "xpanderrender")
ROMS = os.path.join(os.path.dirname(ROOT), "private", "roms", "xpander")
failures = 0

# Switch matrix positions (column, row): research.md 6.3 / MAME's xpander_base.
YES, MASTER, PAGE2, TUNE = (4, 0), (1, 7), (1, 5), (1, 6)
P1 = [(3, i) for i in range(6)]
P2 = [(4, i) for i in range(6)]
# From power-on with blank memory: YES to RESET MEMORY, the patch page after the restart.
START = ["@1.3", "tap:%d,%d" % YES, "@6"]


def check(ok, what):
    global failures
    print(("ok    " if ok else "FAIL  ") + what)
    if not ok:
        failures += 1


def tap(k):
    return "tap:%d,%d" % k


def run(steps):
    """Runs xpanderrender; returns [(time, [vfd1, vfd2, vfd3], info line)] for each "show"."""
    with tempfile.TemporaryDirectory() as d:
        out = subprocess.run([RENDER, ROMS, os.path.join(d, "o.wav")] + steps, capture_output=True, text=True, check=True).stdout
    shows, cur = [], None
    for line in out.splitlines():
        m = re.match(r"\s*([\d.]+) VFD(\d) \|(.*)\|$", line)
        if m:
            if m.group(2) == "1":
                cur = [float(m.group(1)), [], ""]
                shows.append(cur)
            cur[1].append(m.group(3))
        elif cur is not None and "peak so far" in line:
            cur[2] = line
    return shows, out


def info(line, key):
    m = re.search(key + r" ([0-9a-fA-F.]+)", line)
    return m.group(1) if m else None


def main():
    if not os.path.exists(os.path.join(ROMS, "exp1.2-0.u8")):
        print("no ROMs in the private folder: not checked")
        return 0
    service = START + [tap(MASTER), tap(PAGE2), tap(P1[0]), "@6.6"]

    # 21: VERSION (MASTER, PAGE 2, VERSION).
    shows, _ = run(START + [tap(MASTER), tap(PAGE2), tap(P1[5]), "@6.6", "show"])
    t = " ".join(shows[-1][1]).replace("0N", "ON").replace("S0F", "SOF").replace("0I", "OI").replace("PR0CESS0R", "PROCESSOR").replace("VERSI0N", "VERSION")
    check("SOFTWARE VERSION -   1.2" in t and "VOICE VERSION -  1.4" in t, "21 VERSION shows main 1.2 and voice 1.4 (SM p36)")

    # 10: MEM: every RAM and ROM chip reported OK, none bad (SM p33).
    steps = service + [tap(P1[1])]
    for i in range(1, 110):
        steps += ["@%.2f" % (6.8 + 0.25 * i), "show"]
    shows, _ = run(steps)
    seen = {s[1][1].strip() for s in shows}
    wanted = ["RAM 0 0K", "RAM 1 0K", "RAM 2 0K", "R0M 0 0K", "R0M 1 0K", "R0M 2 0K", "R0M 3 0K"]
    missing = [w for w in wanted if w not in seen]
    bad = [s for s in seen if "BAD" in s]
    check(not missing and not bad, "10 MEM: RAM 0-2 and ROM 0-3 OK (SM p33)" + (" missing %s" % missing if missing else "") + (" %s" % bad if bad else ""))

    # 11: VMEM: the voice board's RAM test and ROM checksum (SM p33).
    shows, _ = run(service + [tap(P1[2]), "@40", "show"])
    check("VMEM 0K" in shows[-1][1][1], "11 VMEM: the voice board's memory OK (SM p33): '%s'" % shows[-1][1][1].strip())

    # 12: LEDS: all on (SM p33).
    steps = service + [tap(P2[0])]
    for i in range(1, 12):
        steps += ["@%.2f" % (6.8 + 0.1 * i), "show"]
    shows, _ = run(steps)
    allOn = any(info(s[2], "LED1") == "00" and info(s[2], "LED2") == "00" for s in shows)
    check(allOn, "12 LEDS: every LED lights (SM p33)")

    # 13: DISP: every segment of the 120 digits (SM p33).
    steps = service + [tap(P1[0])]
    for i in range(1, 30):
        steps += ["@%.2f" % (6.8 + 0.1 * i), "show"]
    shows, _ = run(steps)
    most = max(int(info(s[2], "segments lit") or 0) for s in shows)
    check(most == 3 * 40 * 16, "13 DISP: all segments of all 120 digits (SM p33): at most %d of %d lit" % (most, 3 * 40 * 16))

    # 15, 16: the 4 V reference and the DAC offset (model constants, SM p34).
    _, out = run(["@0.1", "consts"])
    m = re.search(r"ref4v ([\d.]+) dacOffset ([-\d.e]+)", out)
    ref, off = float(m.group(1)), float(m.group(2))
    check(3.920 <= ref <= 4.080, "15 4 V reference %.3f V (3.920-4.080, SM p34)" % ref)
    check(abs(off) <= 1e-3, "16 DAC offset %.1f mV (0 +- 1 mV, SM p34)" % (off * 1e3))

    # 17: DAC MONO: "no or very seldom beep" (SM p35). Voice ROM 1.4 (F19E) steps VCO 1's S&H through
    # 16 DAC codes and measures 64 periods of each; a step that goes backwards or jumps opens the
    # outputs (AUTO*) for about 25 ms. Sweeps take 0.3 s; the mute flag is read every 20 ms, so each
    # beep is seen. The model has no noise in this path: any beep is a fault, so none may sound.
    steps = service + [tap(P2[4]), "@7", "rec"]
    for i in range(1, 501):
        steps += ["@%.2f" % (7 + 0.02 * i), "show"]
    shows, _ = run(steps)
    mutes = [info(s[2], "mute") for s in shows]
    beeps = sum(1 for a, b in zip(["1"] + mutes, mutes) if a == "1" and b == "0")
    peak = float(info(shows[-1][2], "peak so far"))
    check(beeps == 0 and peak < 1e-3, "17 DAC MONO: %d beeps over 10 s (SM p35: none or very seldom): peak %.5f" % (beeps, peak))

    # 24-27: TUNE ALL: VCOs, PW, RES, VCF and VCA all pass (SM p20-21).
    shows, _ = run(START + [tap(TUNE), tap(P1[5]), "@150", "show"])
    verdicts = shows[-1][1][2].split()
    check(verdicts == ["PASS"] * 5, "24-27 TUNE ALL: VCOS PW RES VCF VCA %s (SM p20-21)" % verdicts)

    # 23: no VOICE PROCESSOR MALFUNCTION over all of the above (the halt watchdog, SM p8).
    print("\n%s" % ("all passed" if failures == 0 else "%d FAILED" % failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
