// Novation Bass Station Rack: the driver.
#include "machines/drivers.h"
#include "bsrack/plugin/PluginProcessor.h"

namespace lemus::drivers {

const MachineInfo &bsrack()
{
	static const MachineInfo info{
		"bsrack", "Novation Bass Station Rack", "Analogue bass synthesizer module (1995)", Kind::Instrument,
		// The program EPROM, firmware v1.5 (MAME's bassstr set). Needed: the oscillators' clocks,
		// the envelopes, the LFO and the panel are the firmware's.
		{{"v1.5.bin", 32768, "2c2dc3bee0fabf1e77d02cdf3e53885f2c5b913e", "a8bc9721", false, {}, "Firmware ROM"}},
		// The panel's brand marks (optional, from the user): a file per place in artwork/bsrack/, each
		// the box docs/panel.json measures for it. The panel is drawn at up to 16 px/mm (4 px/mm, a
		// window twice the size, a Retina screen).
		{"bsrack.svg", nullptr,
		 {{"maker", "novation", "Novation logo", 29.03f, 2.80f},
		  {"name", "bass_station_rack", "BassStation Rack logo", 42.82f, 5.78f},
		  {"rear_maker", "novation_rear", "Novation logo (rear)", 88.5f, 8.6f},
		  {"rear_name", "bass_station_rack_rear", "BassStation Rack logo (rear)", 87.7f, 11.9f}},
		 16.0f, "no-mono-bsr"},
		[](const RomSet &roms) { return std::make_unique<BassStationProcessor>(roms.find("v1.5.bin"), Artwork::load(roms, bsrack().artwork)); },
		{},
		false,
		"FILTER INPUT"};
	return info;
}

} // namespace lemus::drivers
