// ProCo RAT distortion pedal (1984 "White Face" small box, LM308): the driver. Circuit only, no ROM.
#include "machines/drivers.h"
#include "rat/plugin/HardwareView.h"
#include "rat/plugin/PanelData.h"
#include "rat/plugin/PluginProcessor.h"

namespace lemus::drivers {

const MachineInfo &rat()
{
	using namespace ratpanel;
	static const MachineInfo info{
		"rat", "ProCo RAT", "Distortion pedal (1984)", Kind::Effect, {},
		// The pedal's brand marks (optional, from the user): a file per place in artwork/rat/, each
		// the panel's box for it (docs/panel.json; the tests check they match). The RAT letters and
		// the Pro Co logo are in the top's white block, the Pro Co Sound Inc logo on the front face.
		// The panel is drawn at up to 24 px/mm (a window twice the size on a Retina screen).
		{"rat.svg",
		 nullptr,
		 {{"model", "rat", "RAT", kPlaceRat.w(), kPlaceRat.h()},
		  {"maker", "proco", "Pro Co logo (top)", kPlaceProco.w(), kPlaceProco.h()},
		  {"front", "proco_front", "Pro Co Sound Inc logo (front)", kPlaceFront.w(), kPlaceFront.h()}},
		 4 * HardwareView::kPxPerMm},
		[](const RomSet &roms) { return std::make_unique<RatProcessor>(Artwork::load(roms, rat().artwork)); }};
	return info;
}

} // namespace lemus::drivers
