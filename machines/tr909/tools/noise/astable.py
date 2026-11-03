#!/usr/bin/env python3
"""The noise generator's clock (docs/circuit/noise.md, §3): the IC31a/b astable (HD14070BP at
+15 V; SN p.11 grid M-15) simulated in the time domain: R184 10k from IC31a's output to node X, C47
100p from IC31b's output to X, R185 33k from X into IC31b's input, whose capacitance and input
diodes are modelled; each gate switches at its threshold after its delay, through its output
resistance. Prints the frequency for typical parts, for each parameter's range, and over random
units.

    astable.py [--units N]
"""
import argparse

import numpy as np


def run(R184=10e3, C47=100e-12, R185=33e3, VDD=15.0, vt=0.5, td=40e-9, Ro=170.0, Cin=7e-12, Cx=3e-12, Cy=7e-12,
        Cz=20e-12, rprot=300.0, dt=0.25e-9, periods=12):
	"""Frequency in Hz. vt: the gates' threshold as a share of VDD; td: their delay; Ro: their output
	resistance; Cin: IC31b's input with the board's stray; Cx, Cy, Cz: the strays at X, at IC31b's
	output (with IC31a's input) and at the clock line (with the two 4006 clock inputs); rprot: the
	input diodes' series resistance."""
	VT = vt * VDD
	X = P = Y = 0.0  # node X, IC31b's input (pin 6), IC31b's output (pin 4)
	Z = VDD          # IC31a's output (pin 3), the clock
	yt, zt = 0.0, VDD  # the gates' output targets
	pb, pa = [], []    # pending switchings (time, target)
	t, edges, last = 0.0, [], False
	while t < periods * 4e-6:
		want_b = VDD if P > VT else 0.0
		if (pb[-1][1] if pb else yt) != want_b:
			pb.append((t + td, want_b))
		want_a = 0.0 if Y > VT else VDD
		if (pa[-1][1] if pa else zt) != want_a:
			pa.append((t + td, want_a))
		while pb and pb[0][0] <= t:
			yt = pb.pop(0)[1]
		while pa and pa[0][0] <= t:
			zt = pa.pop(0)[1]
		i184 = (Z - X) / R184
		i185 = (X - P) / R185
		ip = (P - VDD - 0.6) / rprot if P > VDD + 0.6 else (P + 0.6) / rprot if P < -0.6 else 0.0
		iy = (yt - Y) / Ro
		iz = (zt - Z) / Ro
		# X: Cx dX + C47 (dX - dY) = i184 - i185;  Y: Cy dY + C47 (dY - dX) = iy
		a11, a12, b1 = Cx + C47, -C47, i184 - i185
		a21, a22, b2 = -C47, Cy + C47, iy
		det = a11 * a22 - a12 * a21
		dX = (b1 * a22 - a12 * b2) / det
		dY = (a11 * b2 - a21 * b1) / det
		X += dX * dt
		Y += dY * dt
		P += (i185 - ip) / Cin * dt
		Z += (iz - i184) / Cz * dt
		t += dt
		up = Y > VDD / 2
		if up and not last:
			edges.append(t)
		last = up
	e = np.array(edges[2:])
	return 1.0 / np.mean(np.diff(e)) if len(e) > 2 else float('nan')


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("--units", type=int, default=60)
	a = ap.parse_args()
	k = 1e-3
	print(f"typical parts (delays 40 ns, C_in 7 pF, 3 pF at X, outputs 170 ohms): {run() * k:.1f} kHz")
	print(f"gate delays only (no R185 lag): {run(Cin=0.2e-12, dt=0.02e-9) * k:.1f} kHz")
	for name, vals in (("Cin", (6e-12, 8e-12, 10.5e-12)), ("vt", (0.4, 0.6)), ("td", (30e-9, 60e-9)),
	                   ("Ro", (100, 500)), ("R184", (9.5e3, 10.5e3)), ("C47", (90e-12, 110e-12))):
		print(name, " ".join(f"{v:g}: {run(**{name: v}) * k:.1f}" for v in vals), "kHz")
	rng = np.random.default_rng(1)
	f = np.array([run(R184=10e3 * rng.uniform(0.95, 1.05), R185=33e3 * rng.uniform(0.95, 1.05),
	                  C47=100e-12 * rng.uniform(0.9, 1.1), Cin=(rng.uniform(5, 7.5) + rng.uniform(1, 3)) * 1e-12,
	                  Cx=rng.uniform(1, 6) * 1e-12, td=rng.uniform(30, 60) * 1e-9, Ro=rng.uniform(150, 500),
	                  vt=rng.uniform(0.45, 0.55), periods=8) for _ in range(a.units)]) * k
	print(f"{a.units} random units: mean {f.mean():.1f}, 5-95 % {np.percentile(f, 5):.1f}-{np.percentile(f, 95):.1f},"
	      f" range {f.min():.1f}-{f.max():.1f} kHz")


if __name__ == "__main__":
	main()
