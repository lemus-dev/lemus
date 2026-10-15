#pragma once

#include "machine.h"

#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace tb303 {

// Pattern text: steps separated by spaces, each KEY[flags][:TIME].
//   KEY   C C# D D# E F F# G G# A A# B C2 (the panel's 13 keys), or - for a rest
//   flags u (TRANSPOSE UP), d (DOWN), a (ACCENT), s (SLIDE)
//   TIME  N note (default), T tie, R rest
// e.g. "C:N Ca D#u:N C:T -:R Gs A#d"
inline std::vector<Machine::Step> parsePattern(const std::string &text)
{
	static const char *keys[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", "C2"};
	std::vector<Machine::Step> steps;
	std::istringstream ss(text);
	std::string tok;
	while (ss >> tok) {
		Machine::Step st;
		const auto colon = tok.find(':');
		std::string key = tok.substr(0, colon);
		const std::string time = colon == std::string::npos ? "N" : tok.substr(colon + 1);
		std::string flags;
		while (!key.empty() && std::islower(static_cast<unsigned char>(key.back()))) {
			flags += key.back();
			key.pop_back();
		}
		st.time = time == "T" ? Machine::Step::Tie : time == "R" || key == "-" ? Machine::Step::Rest : Machine::Step::Note;
		for (int k = 0; k < 13; ++k)
			if (key == keys[k])
				st.key = uint8_t(k);
		st.up = flags.find('u') != std::string::npos;
		st.down = flags.find('d') != std::string::npos;
		st.accent = flags.find('a') != std::string::npos;
		st.slide = flags.find('s') != std::string::npos;
		steps.push_back(st);
	}
	return steps;
}

// A pattern for the tests and tools to run. The plugin itself starts blank: the 303 has no
// factory patterns.
inline constexpr const char *kTestPattern = "Cd Cd C Cda D#d Cd Gds Cd Cd A#d Cda Fd Cd D#a Cd G";

} // namespace tb303
