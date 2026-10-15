// Artwork packs: the brand marks a machine's panel draws, from an SVG in the user's ROM folder
// (<machine>.svg, loose or in <machine>.zip). Lemus ships no marks: without a pack, panels draw
// none. A machine can give a blank pack, its mark slots commented out, which the host writes into
// the ROM folder for the user to fill in (host/Roms.h); a pack with no marks counts as none.
//
// Any SVG will do. Each mark is the element whose
// id is the mark's name (a path, a shape, text, or a group of them, with any transforms and
// curves), in the SVG's own units, which are the panel's millimetres; the page size and viewBox
// are ignored. Only the outline is used: colours come from the panel.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace lemus {

struct Mark {
	juce::String id;
	juce::Path outline; // panel millimetres, in the frame the panel uses for this mark
};

class Artwork {
public:
	Artwork() = default;

	// Reads a pack; an empty Artwork if `svg` is null or isn't an SVG.
	static Artwork parse(const juce::MemoryBlock *svg)
	{
		Artwork a;
		if (svg == nullptr || svg->isEmpty())
			return a;
		auto xml = juce::XmlDocument::parse(svg->toString());
		if (xml == nullptr || !xml->hasTagNameIgnoringNamespace("svg"))
			return a;
		// The SVG's own units are the panel's millimetres: without a page size or viewBox, JUCE
		// doesn't scale them.
		for (const char *attribute : {"width", "height", "viewBox"})
			xml->removeAttribute(attribute);
		if (auto root = juce::Drawable::createFromSVG(*xml))
			collect(*root, a.m_marks);
		return a;
	}

	const Mark *find(const juce::String &id) const
	{
		for (const auto &m : m_marks)
			if (m.id == id)
				return &m;
		return nullptr;
	}
	const std::vector<Mark> &marks() const { return m_marks; }
	bool empty() const { return m_marks.empty(); }

private:
	// Every element with an id is a mark, groups included (editors wrap everything in layers,
	// so their children are looked at too).
	static void collect(const juce::Component &parent, std::vector<Mark> &out)
	{
		for (auto *child : parent.getChildren())
			if (auto *d = dynamic_cast<const juce::Drawable *>(child)) {
				if (d->getComponentID().isNotEmpty())
					if (auto outline = outlineOf(*d); !outline.isEmpty())
						out.push_back({d->getComponentID(), outline});
				collect(*d, out);
			}
	}

	// A drawable's outline. A group's is its shapes' together, filled even-odd if any of them is.
	static juce::Path outlineOf(const juce::Drawable &d)
	{
		if (dynamic_cast<const juce::DrawableComposite *>(&d) == nullptr)
			return d.getOutlineAsPath();
		juce::Path p;
		bool evenOdd = false;
		for (auto *child : d.getChildren())
			if (auto *cd = dynamic_cast<const juce::Drawable *>(child)) {
				const auto part = outlineOf(*cd);
				evenOdd = evenOdd || !part.isUsingNonZeroWinding();
				p.addPath(part);
			}
		p.setUsingNonZeroWinding(!evenOdd);
		p.applyTransform(d.getTransform());
		return p;
	}

	std::vector<Mark> m_marks;
};

} // namespace lemus
