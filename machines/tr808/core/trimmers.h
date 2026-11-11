#pragma once

namespace tr808 {

// The service notes' six trimmers (SN p.14), each modelled from its part and its circuit: the
// rotation (0 fully anticlockwise, 1 fully clockwise) through the resistance it puts in circuit to
// what it sets. Turned clockwise, each runs its oscillator faster or its level higher.
//   IntClock    main TM1 100k(B), in series with R49 in IC1's oscillator (C11, C203): TP2's period
//   TempoClock  main TM2 10k(B), from -15 V to ground, its wiper through R43 220k into Q14's base:
//               TP3's period with TEMPO and FINE fully clockwise (TempoClock::bpm)
//   CpOffset    main TM3 10k(B), C.P. OFFSET, through R370 47k to Q71's base: cp.cir's offset control
//   Noise       main TM4 22k(B), IC24a's gain leg: the white noise's level (Calibration::noiseRms)
//   Cowbell1/2  voicing TM1 and TM2 220k(B), with R44 and R45 in IC1's squares (C6, C5): TP6, TP7
enum class Trimmer { IntClock, TempoClock, CpOffset, Noise, Cowbell1, Cowbell2 };
constexpr int kTrimmers = 6;

// The parts around them that a unit's serial number changes (core/revisions.h): the late board's
// unless given.
struct TrimParts {
	double r49 = 100e3, c11 = 0.047e-6, c203 = 0.039e-6;           // the INT clock (main board)
	double r44 = 150e3, c6 = 0.022e-6, r45 = 100e3, c5 = 0.018e-6; // the cowbell's squares (voicing)
	bool earlyNoise = false; // the first noise generator: R127 4.7k in series with TM4
	static TrimParts forSerial(int serial);
};

// What trimmer t sets at rotation r: the INT clock's period at TP2 and the tempo clock's at TP3
// (seconds), TM3's rotation (cp.cir's offset control), Calibration::noiseRms and the squares'
// frequencies (Hz). The noise circuit runs with TM4 at mid travel (noise.cir), so TM4 is its gain
// against that, on the noise transistor's level: the nominal unit's, a noiseRms of 0.130 at mid
// travel.
double trimmerValue(Trimmer t, double r, const TrimParts &p);
// The rotation that sets `value`: the end of the travel nearest it when it can't.
double trimmerRotation(Trimmer t, double value, const TrimParts &p);
// TP2's low time each cycle: C11 and C203 discharging through D13 and R48 (seconds).
double intClockLow(double r, const TrimParts &p);

} // namespace tr808
