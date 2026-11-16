#pragma once

#include "machine.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace sh101 {

// MIDI to the SH-101's keyboard. Notes press keys in the CPU's key matrix and the firmware does
// the rest: note priority, gate and retrigger, arpeggio, step entry. MIDI note 53 (F3) is the
// lowest key, so with RANGE 8' and TRANSPOSE M a note plays at its own pitch.
//
// The driver plays the keyboard the way fingers would, within what the firmware can see
// (docs/sequencer.md, "Keyboard"):
// - The CPU latches the keys once per main loop (1.6-3.7 ms), scans them in the next loop, and
//   counts a release only on the second empty scan. So every key change is held for kHoldLoops
//   loop boundaries before the key may change again; a shorter release would not be seen.
// - A note-on for a key that is still down (a repeated note, note-off and note-on at the same
//   instant, or a second note-on) lifts the key first and presses it again once the release has
//   registered: the gate drops for one loop, as when a player repeats a key.
// - With an arpeggio mode on, a chord that arrives all at once is pressed one key first (the
//   lowest; the highest for DOWN) and the rest kHoldLoops later. A real chord is never exactly
//   simultaneous, and the firmware starts the pattern from the key it saw first: pressed in
//   one scan, the first step would be whatever note played last.
//
// Notes outside the 32 keys move the TRANSPOSE switch an octave down or up from the panel's
// setting for as long as they are the newest note, the way a player would; notes beyond what
// L and H reach are folded by octaves.
class KeyboardDriver {
public:
	static constexpr int kLowestNote = 53;
	static constexpr int64_t kHoldLoops = 2;

	void noteOn(int note)
	{
		m_settled = false;
		erase(m_held, note);
		m_held.push_back(note);
		erase(m_fresh, note);
		m_fresh.push_back(note);
	}
	void noteOff(int note)
	{
		m_settled = false;
		erase(m_held, note);
		erase(m_fresh, note);
	}
	void allNotesOff()
	{
		m_settled = false;
		m_held.clear();
		m_fresh.clear();
	}
	bool anyHeld() const { return !m_held.empty(); }

	// Once per host sample, before the machine runs: works out the wanted key states and
	// transpose and applies the changes the firmware will see. `panel` is the TRANSPOSE switch on
	// the panel.
	void tick(Machine &machine, Transpose panel)
	{
		// Settled: every key as the notes want it and TRANSPOSE where it ends up. Nothing changes
		// then until a note or the panel's TRANSPOSE does.
		if (m_settled && int(panel) == m_settledPanel)
			return;
		const auto loops = int64_t(machine.loops());
		auto allowed = [loops](int64_t since) { return loops < since || loops - since >= kHoldLoops; };

		// With no notes held, the switch goes back to the panel's position once the last release
		// has registered: the firmware recomputes the KCV of a key it still counts as held (for a
		// loop after it lifts) from the switch.
		const bool anyDown = std::find(m_down.begin(), m_down.end(), true) != m_down.end();
		int transpose = m_transpose;
		if (!m_held.empty())
			transpose = transposeFor(m_held.back(), int(panel));
		else if (!anyDown && loops - m_lastKeyChange > kHoldLoops)
			transpose = int(panel);
		if (transpose != m_transpose && allowed(m_transposeSince)) {
			m_transpose = transpose;
			m_transposeSince = loops;
			machine.setTranspose(Transpose(transpose));
		}

		std::array<bool, Machine::kKeys> want{};
		for (int note : m_held)
			want[size_t(keyFor(note, m_transpose, int(panel)))] = true;
		for (int note : m_fresh) {
			const int k = keyFor(note, m_transpose, int(panel));
			if (m_down[size_t(k)])
				m_repress[size_t(k)] = true;
		}
		m_fresh.clear();

		// A chord into an idle arpeggio: one key now, the others after it has registered.
		const bool arp = machine.ledOn(Machine::Led::ArpUp) || machine.ledOn(Machine::Led::ArpUpDown) ||
		                 machine.ledOn(Machine::Led::ArpDown);
		if (arp && !anyDown && std::count(want.begin(), want.end(), true) > 1) {
			const bool down = machine.ledOn(Machine::Led::ArpDown);
			int first = -1;
			for (int k = 0; k < Machine::kKeys; ++k)
				if (want[size_t(k)] && (first < 0 || down))
					first = k;
			m_first = first;
			m_firstSince = loops;
		}
		if (!arp)
			m_first = -1;

		for (int k = 0; k < Machine::kKeys; ++k) {
			bool w = want[size_t(k)];
			if (m_repress[size_t(k)]) {
				if (!m_down[size_t(k)])
					m_repress[size_t(k)] = false;
				else
					w = false;
			}
			if (w && !m_down[size_t(k)] && m_first >= 0 && k != m_first && !allowed(m_firstSince))
				w = false;
			if (w == m_down[size_t(k)] || !allowed(m_since[size_t(k)]))
				continue;
			m_down[size_t(k)] = w;
			m_since[size_t(k)] = loops;
			m_lastKeyChange = loops;
			machine.setKey(k, w);
		}

		bool settled = m_first < 0 && want == m_down;
		for (int k = 0; settled && k < Machine::kKeys; ++k)
			settled = !m_repress[size_t(k)];
		const bool down = std::find(m_down.begin(), m_down.end(), true) != m_down.end();
		settled = settled && m_transpose == (m_held.empty() ? (down ? m_transpose : int(panel))
		                                                     : transposeFor(m_held.back(), int(panel)));
		m_settled = settled;
		m_settledPanel = int(panel);
	}

private:
	static void erase(std::vector<int> &v, int note) { v.erase(std::remove(v.begin(), v.end(), note), v.end()); }
	// The TRANSPOSE position (0 L, 1 M, 2 H) that puts `note` on the keyboard, nearest the panel's.
	static int transposeFor(int note, int panel)
	{
		const int v = note - kLowestNote; // key index at the panel's transpose
		for (int d : {0, -1, 1, -2, 2}) {
			const int t = panel + d;
			if (t < 0 || t > 2)
				continue;
			const int k = v - 12 * (t - panel);
			if (k >= 0 && k < Machine::kKeys)
				return t;
		}
		return v < 0 ? 0 : 2;
	}
	static int keyFor(int note, int transpose, int panel)
	{
		int k = note - kLowestNote - 12 * (transpose - panel);
		while (k < 0)
			k += 12;
		while (k >= Machine::kKeys)
			k -= 12;
		return k;
	}
	static std::array<int64_t, Machine::kKeys> never()
	{
		std::array<int64_t, Machine::kKeys> a;
		a.fill(INT64_MIN / 2);
		return a;
	}

	std::vector<int> m_held;  // in the order pressed
	std::vector<int> m_fresh; // note-ons since the last tick
	std::array<bool, Machine::kKeys> m_down{};
	std::array<bool, Machine::kKeys> m_repress{};
	std::array<int64_t, Machine::kKeys> m_since = never(); // loop count at the key's last change
	int m_transpose = int(Transpose::Mid);
	int64_t m_transposeSince = INT64_MIN / 2;
	int64_t m_lastKeyChange = INT64_MIN / 2;
	int m_first = -1; // the arpeggio chord's first key, while the others wait
	int64_t m_firstSince = INT64_MIN / 2;
	bool m_settled = false;
	int m_settledPanel = -1;
};

} // namespace sh101
