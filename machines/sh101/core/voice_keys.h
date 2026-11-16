#pragma once

#include "lines.h"

#include <algorithm>
#include <vector>

namespace sh101 {

// Voice mode: MIDI straight to the synth board's lines, for when there is no CPU ROM. It does what
// the firmware does in normal playing (docs/sequencer.md, "Keyboard"), so a note sounds as it would
// with the ROM:
// - KCV = the key + 12 x TRANSPOSE + 5, with MIDI note 53 (F3) the lowest key at the panel's
//   TRANSPOSE; a note beyond the keys moves TRANSPOSE as the MIDI keyboard driver does, and notes
//   beyond L and H fold by octaves. The RANGE channel is the RANGE switch alone (12 x position).
// - Priority: GATE+TRIG plays the newest note, falling back to the lowest held; GATE and LFO the
//   lowest. A new note while another is held changes the pitch without a gap; a repeated note
//   drops the gate for about one firmware loop (kGapSeconds), as lifting and pressing the key does.
// - The HOLD pedal keeps the gate on and the last note after the keys are up.
// - CLK RST pulses for one loop at the first note with S7 = LFO.
// - PORTA OFF stays low (no arpeggio) and the RANDOM sample-and-hold keeps its power-on value: it is
//   the firmware that steps it on each clock.
class VoiceKeys {
public:
	static constexpr int kLowestNote = 53;
	static constexpr double kGapSeconds = 0.003;   // a repeated key: the gate's gap
	static constexpr double kClkRstSeconds = 0.002; // one main loop

	void prepare(double rate)
	{
		m_gapSamples = std::max(1, int(kGapSeconds * rate + 0.5));
		m_clkRstSamples = std::max(1, int(kClkRstSeconds * rate + 0.5));
	}
	void reset()
	{
		m_held.clear();
		m_pedal = false;
		m_newest = m_sounding = -1;
		m_gap = m_clkRst = 0;
		m_lines = CpuLines{};
		m_changed = true;
	}

	void noteOn(int note, Trigger trigger)
	{
		// Lifted and pressed again (a repeat, or note-off and note-on at once): the gate drops for a
		// loop. With the pedal holding the gate it stays on.
		if (m_lines.gate && note == m_sounding && !m_pedal)
			m_gap = m_gapSamples;
		if (m_held.empty() && !(m_pedal && m_lines.gate) && trigger == Trigger::Lfo)
			m_clkRst = m_clkRstSamples;
		m_held.erase(std::remove(m_held.begin(), m_held.end(), note), m_held.end());
		m_held.push_back(note);
		m_newest = note;
	}
	void noteOff(int note)
	{
		m_held.erase(std::remove(m_held.begin(), m_held.end(), note), m_held.end());
		if (note == m_newest)
			m_newest = -1;
	}
	void allNotesOff()
	{
		m_held.clear();
		m_newest = -1;
		m_pedal = false;
	}
	void setPedal(bool held) { m_pedal = held; }

	// Once per sample. Returns true if lines() changed.
	bool tick(Range range, Trigger trigger, Transpose panel)
	{
		if (!m_held.empty())
			m_sounding = trigger == Trigger::GateTrig && m_newest >= 0 ? m_newest
			                                                           : *std::min_element(m_held.begin(), m_held.end());
		CpuLines l = m_lines;
		l.range = 12 * (int(range) + 1);
		if (m_sounding >= 0)
			l.kcv = kcvFor(m_sounding, int(panel)); // the S/H keeps the last note
		l.gate = (!m_held.empty() || (m_pedal && m_lines.gate)) && m_gap == 0;
		l.clkRst = m_clkRst > 0;
		l.portaOff = false;
		if (m_gap > 0)
			--m_gap;
		if (m_clkRst > 0)
			--m_clkRst;
		const bool changed = m_changed || l.kcv != m_lines.kcv || l.range != m_lines.range || l.gate != m_lines.gate ||
		                     l.clkRst != m_lines.clkRst;
		m_lines = l;
		m_changed = false;
		return changed;
	}
	const CpuLines &lines() const { return m_lines; }

	// The KCV code the keyboard gives `note` with the panel's TRANSPOSE (0 L, 1 M, 2 H).
	static int kcvFor(int note, int panel)
	{
		int kcv = note - kLowestNote + 12 * panel + 5;
		while (kcv < 5)
			kcv += 12;
		while (kcv > 31 + 24 + 5)
			kcv -= 12;
		return kcv;
	}

private:
	std::vector<int> m_held; // in the order pressed
	bool m_pedal = false;
	int m_newest = -1;       // the last note pressed, while it is held
	int m_sounding = -1;     // the note on KCV, -1 before the first
	int m_gap = 0, m_clkRst = 0;
	int m_gapSamples = 132, m_clkRstSamples = 88;
	CpuLines m_lines;
	bool m_changed = true;
};

} // namespace sh101
