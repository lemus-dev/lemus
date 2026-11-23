// Bringing a machine up as a new unit leaves the factory: the firmware's own steps, run headless.
// With blank battery RAM the firmware asks RESET MEMORY? (YES), then TUNE ALL measures and
// corrects every VCO, pulse width, resonance and filter through the OSC comparator, and stores
// the corrections in battery RAM. With the factory patches (Oberheim's SysEx dump of single
// patches 00-99), the firmware then receives them through MIDI IN, as from another Xpander or a
// librarian, and starts on patch 00. The panel text is read through main ROM 1.2's font table.
#pragma once

#include "machine.h"

#include <functional>
#include <string>
#include <vector>

namespace xpander {

// The files a factory state is kept in (Lemus's application folder, xpander/), without and with the
// factory patches: the revision goes up whenever the analog model changes what TUNE ALL stores, so
// an old tune isn't reused.
inline constexpr const char* kFactoryFile = "factory-r13.ram";
inline constexpr const char* kFactoryPatchesFile = "factory-r13-patches.ram";

// The text display d (0-2) shows; '#' where a pattern isn't a character of the font.
std::string displayText(const uint8_t* mainRom, const MainBoard::Segments& segments, int d);

struct WarmUp
{
    bool ok = false;
    std::string message; // the tune page's verdict, e.g. "PASS"
    double seconds = 0;  // emulated
    std::string patch;   // with patches: what the single patch page shows after them, e.g. "S00 SYNC0M 8"
};

// From power-on with blank RAM to a reset, tuned unit on its single-patch page. `patches`: SysEx
// single patch dumps (F0 10 02 01 00 nn ... F7) for the firmware to store, or empty. `cancel` is
// polled.
WarmUp warmUp(Machine& m, const uint8_t* mainRom, const std::function<bool()>& cancel = {},
              const std::vector<uint8_t>& patches = {});

// Power-on and the firmware's start (about 3 s of its time), the voices not computed.
void boot(Machine& m);

} // namespace xpander
