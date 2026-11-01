// Artwork: the brand marks a machine's panel draws, from the user's ROM folder. Lemus ships no
// marks: without artwork, panels draw none. Two kinds (host/Roms.h finds both):
//
// - A place's own file (machines that list places, ArtworkSpec::places): one SVG or PNG per place
//   on the panel, in artwork/<machine>/ in the ROM folder (or artwork/<machine>.zip), as MAME keeps
//   artwork, drawn as it looks, scaled to fit the place's box with its proportions kept and
//   centred in it. Only what it draws counts: a page's margins and a PNG's transparent border
//   don't. Transparent parts show the panel. The Artwork pop-up on the bar lists the places.
// - An artwork pack, the older kind: one SVG (<machine>.svg, loose or in <machine>.zip). Each mark
//   is the element whose id is the mark's name (a path, a shape, text, or a group of them, with any
//   transforms and curves), in the SVG's own units, which are the panel's millimetres; the page
//   size and viewBox are ignored. Only the outline is used: colours come from the panel. It still
//   fills the places that have no file of their own. A machine can give a blank pack, its mark
//   slots commented out, which the host writes into the ROM folder for the user to fill in if the
//   machine has no places; a pack with no marks counts as none.
#pragma once

#include "Roms.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

namespace lemus {

// A place's file: an SVG or a PNG, drawn as it looks.
class Picture {
public:
	static constexpr int kMaxSide = 8192; // pixels, for a PNG

	// Told apart by their content, whatever the file is called. Null if it's neither, Lemus can't
	// read it, a PNG is over kMaxSide pixels a side, or it draws nothing.
	static std::shared_ptr<const Picture> load(const juce::MemoryBlock &data)
	{
		auto p = std::make_shared<Picture>();
		static const char png[] = "\x89PNG\r\n\x1a\n";
		if (data.getSize() > 24 && std::memcmp(data.getData(), png, 8) == 0) {
			// Its size first, from the header: a small file can be a huge image.
			const auto *b = static_cast<const uint8_t *>(data.getData());
			const auto be = [b](int at) { return (uint32_t(b[at]) << 24) | (uint32_t(b[at + 1]) << 16) | (uint32_t(b[at + 2]) << 8) | uint32_t(b[at + 3]); };
			if (be(16) == 0 || be(20) == 0 || be(16) > uint32_t(kMaxSide) || be(20) > uint32_t(kMaxSide))
				return nullptr;
			juce::MemoryInputStream in(data, false);
			auto image = juce::PNGImageFormat().decodeImage(in);
			if (!image.isValid())
				return nullptr;
			image = image.convertedToFormat(juce::Image::ARGB);
			const auto drawn = visiblePixels(image);
			if (drawn.isEmpty())
				return nullptr;
			image = image.getClippedImage(drawn).createCopy();
			p->m_content = image.getBounds().toFloat();
			// Halved down to small sizes, so a large PNG drawn small stays smooth.
			p->m_levels.push_back(image);
			while (image.getWidth() >= 32 && image.getHeight() >= 8) {
				image = image.rescaled(image.getWidth() / 2, image.getHeight() / 2, juce::Graphics::highResamplingQuality);
				p->m_levels.push_back(image);
			}
			return p;
		}
		const auto xml = juce::XmlDocument::parse(data.toString());
		if (xml == nullptr || !xml->hasTagNameIgnoringNamespace("svg"))
			return nullptr;
		p->m_svg = juce::Drawable::createFromSVG(*xml);
		if (p->m_svg == nullptr)
			return nullptr;
		// What it draws, found by drawing it: an editor's page frame, an empty layer or a clip path
		// have bounds but draw nothing. Coarse over all it has, then fine over what showed.
		const auto all = p->m_svg->getDrawableBounds().transformedBy(p->m_svg->getTransform());
		if (all.isEmpty())
			return nullptr;
		const auto coarse = visibleArea(*p->m_svg, all.expanded(all.getWidth() * 0.05f, all.getHeight() * 0.05f), 1024);
		if (coarse.isEmpty())
			return nullptr;
		const float margin = juce::jmax(all.getWidth(), all.getHeight()) * 1.1f / 1024.0f * 2.0f;
		p->m_content = visibleArea(*p->m_svg, coarse.expanded(margin), 2048);
		return p->m_content.isEmpty() ? nullptr : p;
	}

	bool isSvg() const { return m_svg != nullptr; }
	// What it draws, in its own units (an SVG's, a PNG's pixels after the transparent border).
	juce::Rectangle<float> content() const { return m_content; }

	// Draws it as it looks, scaled to fit `box` with its proportions kept, and centred in it.
	void drawWithin(juce::Graphics &g, juce::Rectangle<float> box) const
	{
		if (m_content.isEmpty() || box.isEmpty())
			return;
		const auto fit = juce::RectanglePlacement(juce::RectanglePlacement::centred).getTransformToFit(m_content, box);
		if (m_svg != nullptr) {
			m_svg->draw(g, 1.0f, fit);
			return;
		}
		// The smallest level still at least as wide as the pixels it covers.
		const float wide = m_content.getWidth() * std::abs(fit.mat00) * g.getInternalContext().getPhysicalPixelScaleFactor(); // fit only scales and moves
		size_t k = 0;
		while (k + 1 < m_levels.size() && float(m_levels[k + 1].getWidth()) >= wide)
			++k;
		const auto &image = m_levels[k];
		juce::Graphics::ScopedSaveState s(g);
		g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
		g.drawImageTransformed(image,
		                       juce::AffineTransform::scale(m_content.getWidth() / float(image.getWidth()), m_content.getHeight() / float(image.getHeight()))
		                           .followedBy(fit));
	}

private:
	// The pixels with any opacity.
	static juce::Rectangle<int> visiblePixels(const juce::Image &image)
	{
		const juce::Image::BitmapData bd(image, juce::Image::BitmapData::readOnly);
		int x0 = bd.width, y0 = bd.height, x1 = -1, y1 = -1;
		for (int y = 0; y < bd.height; ++y)
			for (int x = 0; x < bd.width; ++x)
				if (bd.getPixelColour(x, y).getAlpha() > 0) {
					x0 = std::min(x0, x);
					x1 = std::max(x1, x);
					y0 = std::min(y0, y);
					y1 = std::max(y1, y);
				}
		return x1 < 0 ? juce::Rectangle<int>() : juce::Rectangle<int>(x0, y0, x1 + 1 - x0, y1 + 1 - y0);
	}

	// Where in `area` (the drawable's units) it draws, drawn `px` pixels on the long side.
	static juce::Rectangle<float> visibleArea(const juce::Drawable &d, juce::Rectangle<float> area, int px)
	{
		const float scale = float(px) / juce::jmax(area.getWidth(), area.getHeight());
		juce::Image image(juce::Image::ARGB, juce::jmax(1, int(std::ceil(area.getWidth() * scale))), juce::jmax(1, int(std::ceil(area.getHeight() * scale))), true,
		                  juce::SoftwareImageType());
		{
			juce::Graphics g(image);
			d.draw(g, 1.0f, juce::AffineTransform::translation(-area.getX(), -area.getY()).scaled(scale));
		}
		const auto r = visiblePixels(image).toFloat();
		return r.isEmpty() ? juce::Rectangle<float>() : (r / scale) + area.getPosition();
	}

	std::unique_ptr<juce::Drawable> m_svg;
	std::vector<juce::Image> m_levels; // a PNG without its transparent border, then halved
	juce::Rectangle<float> m_content;
};

struct Mark {
	juce::String id;
	juce::Path outline; // from a pack: panel millimetres, in the frame the panel uses for this mark
	std::shared_ptr<const Picture> picture; // from the place's own file, drawn as it looks
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

	// A machine with places: its pack (if any), with each place that has a file of its own taking
	// that instead. `roms` holds them as host/Roms.h's addArtwork put them.
	static Artwork load(const RomSet &roms, const ArtworkSpec &spec)
	{
		auto a = parse(roms.find(spec.name));
		for (const auto &place : spec.places)
			if (const auto *data = roms.find(ArtworkSpec::key(place.id)))
				if (auto picture = Picture::load(*data)) {
					auto &m = a.m_marks;
					m.erase(std::remove_if(m.begin(), m.end(), [&](const Mark &x) { return x.id == place.mark; }), m.end());
					m.push_back({place.mark, {}, std::move(picture)});
				}
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

// A machine with places (its processor): when the artwork folder changes (Rescan, or the Artwork
// pop-up opening), the host hands it the new artwork (message thread), then builds its editor again.
class ArtworkUser {
public:
	virtual ~ArtworkUser() = default;
	virtual void setArtwork(Artwork artwork) = 0;
};

} // namespace lemus
