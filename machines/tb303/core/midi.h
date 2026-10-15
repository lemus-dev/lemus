#pragma once

#include <algorithm>
#include <vector>

namespace tb303 {

// What the 303's CPU drives into the voice board: a pitch code for the DAC, GATE, ACCENT
// and SLIDE. Pitch is a MIDI note number here; the voice turns it into the DAC voltage.
struct CpuLines {
	int note = 36;
	bool gate = false;
	bool accent = false;
	bool slide = false;
};

// MIDI to CPU lines, the way Roland's own TB-303 software, the TB-03 and the TD-3 take MIDI:
// velocity 100 or more is an accent (velocity is otherwise ignored), and a note that starts
// while another is held slides to its pitch without retriggering, with GATE kept high.
// Releasing the newest of several held notes slides back to the previous one.
class MidiDriver {
public:
	static constexpr int kAccentVelocity = 100;

	const CpuLines &lines() const { return m_lines; }

	void noteOn(int note, int velocity)
	{
		if (velocity <= 0) {
			noteOff(note);
			return;
		}
		const bool legato = !m_held.empty();
		m_held.erase(std::remove(m_held.begin(), m_held.end(), note), m_held.end());
		m_held.push_back(note);
		m_lines.note = note;
		m_lines.accent = velocity >= kAccentVelocity;
		m_lines.slide = legato;
		m_lines.gate = true;
		if (!legato)
			++m_triggers;
	}

	void noteOff(int note)
	{
		const bool wasCurrent = !m_held.empty() && m_held.back() == note;
		m_held.erase(std::remove(m_held.begin(), m_held.end(), note), m_held.end());
		if (m_held.empty()) {
			m_lines.gate = false;
		} else if (wasCurrent) {
			m_lines.note = m_held.back();
			m_lines.slide = true;
		}
	}

	void allNotesOff()
	{
		m_held.clear();
		m_lines.gate = false;
	}

	// Counts note starts that raise GATE, so the voice can retrigger even when a new note
	// arrives in the same sample as the previous note's release.
	unsigned triggers() const { return m_triggers; }

private:
	CpuLines m_lines;
	std::vector<int> m_held;
	unsigned m_triggers = 0;
};

} // namespace tb303
