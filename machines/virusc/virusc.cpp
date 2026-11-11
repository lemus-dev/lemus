// Access Virus C: the driver.
#include "machines/drivers.h"
#include "virusc/plugin/PluginProcessor.h"

namespace lemus::drivers {

const MachineInfo &virusc()
{
	static const MachineInfo info{
		"virusc", "Access Virus C", "Virtual analog synthesizer (2002)", Kind::Instrument,
		// MAME's virusc set, BIOS v6.6: the one flash image, holding the 80C515's program, the
		// DSP's and the factory sounds. Without it there is nothing to run: the sound engine is
		// the firmware itself.
		{{"virus_c_660x368.bin", 524288, "4c34546889d90522a8d85300c471f18339f88f72", "923ed4bc", false, {}, "Firmware ROM"}},
		// The panel's brand marks (optional, from the user): a file per place in artwork/virusc/, each
		// the panel's logo box (docs/panel.json). The panel is drawn at up to 10.4 px/mm (2.6 px/mm, a
		// window twice the size, a Retina screen).
		{"virusc.svg",
		 nullptr,
		 {{"maker", "access", "Access logo", 51.0f, 9.38f},
		  {"name", "virus_c", "Virus C", 73.38f, 8.0f},
		  {"tagline", "advanced_simulated_analog_synthesizer", "Advanced Simulated Analog Synthesizer", 76.0f, 3.62f}},
		 10.4f},
		[](const RomSet &roms) { return std::make_unique<VirusCProcessor>(roms.find("virus_c_660x368.bin"), Artwork::load(roms, virusc().artwork)); },
		// the rear panel's other outputs and its two inputs (VirusCProcessor's buses)
		{"OUT 2", "OUT 3"},
		true,
		"Input"};
	return info;
}

} // namespace lemus::drivers
