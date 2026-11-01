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
//
// Two held notes of the same pitch count as two, and a note-off ends the older one, so an
// overlapping note of the same pitch slides (ties) instead of being cut by the first note's
// note-off (the TD-3 fixed the same in firmware 1.2.6). While the sustain pedal (CC 64) is down,
// note-offs wait until it comes up, so every note slides from the one before, the same pitch
// included: that's how a DAW that can't overlap two notes of one pitch writes an accented slide
// into the same note, and what D16's Phoscyon sends for one.
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
		if (m_sustain)
			m_waiting.push_back(note);
		else
			release(note);
	}

	void setSustain(bool down)
	{
		if (down == m_sustain)
			return;
		m_sustain = down;
		if (!down) {
			for (const int note : m_waiting)
				release(note);
			m_waiting.clear();
		}
	}

	void allNotesOff()
	{
		m_held.clear();
		m_waiting.clear();
		m_sustain = false;
		m_lines.gate = false;
	}

	// Counts note starts that raise GATE, so the voice can retrigger even when a new note
	// arrives in the same sample as the previous note's release.
	unsigned triggers() const { return m_triggers; }

private:
	// Ends the oldest held note of that pitch.
	void release(int note)
	{
		const auto it = std::find(m_held.begin(), m_held.end(), note);
		if (it == m_held.end())
			return;
		const bool wasCurrent = it == m_held.end() - 1;
		m_held.erase(it);
		if (m_held.empty()) {
			m_lines.gate = false;
		} else if (wasCurrent) {
			m_lines.note = m_held.back();
			m_lines.slide = true;
		}
	}

	CpuLines m_lines;
	std::vector<int> m_held;    // oldest first; a pitch can be there more than once
	std::vector<int> m_waiting; // note-offs held back by the sustain pedal
	bool m_sustain = false;
	unsigned m_triggers = 0;
};

} // namespace tb303
