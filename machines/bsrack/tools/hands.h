// Moving the Bass Station Rack's pots as hands do, for the tools. v1.5 keeps a program's stored value
// for each pot until the pot is moved (soft takeover: a reading that moves more than 5 codes from the
// last one that acted), takes simultaneous moves of many pots badly, and locks up for good when all
// 17 pots are "active" at once (docs/firmware.md, "A lock-up in v1.5": the machine is faithful to
// it). So moveTo turns only the pots whose setting changes, one at a time, rendering in between; a
// tool asks for the few pots it needs (a pot left alone keeps the program's value).
#pragma once

#include "bsrack/core/machine.h"

#include <vector>

namespace bsrack::tools {

inline void moveTo(Machine &m, Controls &now, const Controls &target, double rate, double stepSeconds = 0.06)
{
	std::vector<float> scratch(size_t(rate * stepSeconds) + 1);
	auto render = [&] { m.render(scratch.data(), int(scratch.size()), nullptr); };
	now.switches = target.switches;
	m.setControls(now);
	render();
	for (int p = 0; p < kPots; ++p) {
		if (now.pots[size_t(p)] == target.pots[size_t(p)])
			continue;
		now.pots[size_t(p)] = target.pots[size_t(p)];
		m.setControls(now);
		if (p != kVolume) // analog, after the VCA: not the firmware's
			render();
	}
	render();
}

} // namespace bsrack::tools
