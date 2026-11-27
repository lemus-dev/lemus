#!/usr/bin/env python3
"""Every Bass Station Rack netlist against ngspice: runs compare_circuit.py on each case in tools/spice/cases/
(one JSON file per block page: a list of {"name", "netlist", "args"}, netlists relative to
core/netlists/; optional "probes": outputs left out of the comparison, "known": why a case is
expected to differ, reported as KNWN, and "env": solver settings) and summarises the largest difference per output, in % of the output's range.

    suite.py [--only NAME,...] [--jobs N] [--out DIR] [--limit PCT]

circuitrun comes from $CIRCUITRUN, else the newer of build/bsrack/bsrackcircuitrun in the clone or beside it. Each case keeps its decks and data
under DIR/<case>/ (default build/bsrack/spice). Exit status 1 if any case fails to run or any output
differs by more than --limit (default 1 %) at its worst sample and by more than --rms-limit
(default 0.5 %) in RMS.
"""
import argparse
import concurrent.futures as cf
import glob
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MACHINE = os.path.normpath(os.path.join(HERE, "../.."))
REPO = os.path.normpath(os.path.join(MACHINE, "../../.."))
NET = os.path.join(MACHINE, "core/netlists")
CMP = os.path.join(HERE, "compare_circuit.py")
# Settings a page's netlists need on the solver (the case's own "env" adds to them): none yet.
PAGE_ENV = {}


def cases():
    out = []
    for path in sorted(glob.glob(os.path.join(HERE, "cases", "*.json"))):
        with open(path) as f:
            for c in json.load(f):
                c["page"] = os.path.splitext(os.path.basename(path))[0]
                out.append(c)
    return out


def run(case, out_dir):
    nets = ",".join(os.path.join(NET, n) for n in case["netlist"].split(","))
    keep = os.path.join(out_dir, case["name"])
    os.makedirs(keep, exist_ok=True)
    env = dict(os.environ, **PAGE_ENV.get(case["page"], {}), **case.get("env", {}))
    r = subprocess.run([sys.executable, CMP, nets, *case.get("args", []), "--keep", keep], capture_output=True, text=True, env=env)
    rows = []
    for line in r.stdout.splitlines():
        m = re.match(r"(\S+)\s+range\s+([\d.e+-]+) V\s+max diff\s+([\d.e+-]+) V \(\s*([\d.e+-]+) %\)\s+rms\s+([\d.e+-]+) V", line)
        if m:
            # an output that hardly moves in a case (a comparator's unused side) is measured against
            # 50 mV rather than its own range
            rng = max(float(m.group(2)), 0.05)
            if m.group(1) not in case.get("probes", []):
                rows.append((m.group(1), rng, float(m.group(3)), 100 * float(m.group(3)) / rng, 100 * float(m.group(5)) / rng))
    return case, r.returncode, rows, r.stderr[-2000:]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--out", default=os.path.join(REPO, "build/bsrack/spice"))
    ap.add_argument("--limit", type=float, default=1.0)
    ap.add_argument("--rms-limit", type=float, default=0.5)
    args = ap.parse_args()
    only = set(filter(None, args.only.split(",")))
    todo = [c for c in cases() if not only or c["name"] in only or c["page"] in only]
    bad = 0
    with cf.ThreadPoolExecutor(args.jobs) as ex:
        for case, code, rows, err in ex.map(lambda c: run(c, args.out), todo):
            worst = max((r[3] for r in rows), default=float("nan"))
            rms = max((r[4] for r in rows), default=float("nan"))
            # an output that switches (a comparator, a trigger, a reset) differs on the samples its
            # edge falls in, by up to its swing: its RMS is the measure there
            ok = bool(rows) and (worst <= args.limit or rms <= args.rms_limit)
            known = not ok and "known" in case
            bad += not ok and not known
            mark = "ok  " if ok else "KNWN" if known else "FAIL"
            print(f"{mark} {case['page']:10s} {case['name']:32s} max {worst:7.3f} %  rms {rms:6.3f} %")
            if known:
                print(f"     known: {case['known']}")
            if not rows:
                print("     " + err.strip().replace("\n", "\n     "))
    print(f"\n{len(todo) - bad}/{len(todo)} cases within {args.limit} % (or {args.rms_limit} % RMS)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
