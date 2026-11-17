// keyhost on Windows: computer keys by place, end to end, in a built Lemus VST3 (CMakeLists.txt
// says what it checks). The keys go in as real input (SendInput scan codes), so Windows turns
// them into WM_KEYDOWN/WM_CHAR through this thread's layout, as it does for a DAW's.
//   US Y place: scan code 0x15;  US Z place: scan code 0x2C.
// usage: keyhost VST3 PLUGIN.vst3 OUTDIR CLICKX CLICKY   (click in editor pixels)
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "win.h"

#include <cstdio>
#include <vector>

namespace {

void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

struct Shot
{
	int w = 0, h = 0;
	std::vector<uint8_t> rgb;
};

bool g_fromScreen = false;

Shot shoot(void *hwnd, const juce::String &path)
{
	Shot s;
	std::vector<unsigned char> bgra;
	if (!win::capture(hwnd, g_fromScreen, s.w, s.h, bgra))
		return {};
	s.rgb.resize(size_t(s.w * s.h * 3));
	juce::Image img(juce::Image::RGB, s.w, s.h, false);
	{
		juce::Image::BitmapData d(img, juce::Image::BitmapData::writeOnly);
		for (int y = 0; y < s.h; ++y)
			for (int x = 0; x < s.w; ++x) {
				const unsigned char *p = &bgra[size_t((y * s.w + x) * 4)];
				uint8_t *q = &s.rgb[size_t((y * s.w + x) * 3)];
				q[0] = p[2];
				q[1] = p[1];
				q[2] = p[0];
				d.setPixelColour(x, y, juce::Colour(p[2], p[1], p[0]));
			}
	}
	juce::File f(path);
	f.deleteFile();
	juce::FileOutputStream out(f);
	juce::PNGImageFormat().writeImageToStream(img, out);
	return s;
}

int pixelDiff(const Shot &a, const Shot &b, size_t i)
{
	return std::abs(a.rgb[i * 3] - b.rgb[i * 3]) + std::abs(a.rgb[i * 3 + 1] - b.rgb[i * 3 + 1]) + std::abs(a.rgb[i * 3 + 2] - b.rgb[i * 3 + 2]);
}

double meanLevel(const Shot &s)
{
	double t = 0;
	for (auto v : s.rgb)
		t += v;
	return s.rgb.empty() ? 0 : t / double(s.rgb.size());
}

void *g_hwnd = nullptr;
juce::Point<int> g_focusAt;

void focus()
{
	win::toFront(g_hwnd);
	pump(200);
	win::mouse(g_focusAt.x, g_focusAt.y, true);
	pump(120);
	win::mouse(g_focusAt.x, g_focusAt.y, false);
	pump(300);
}

struct Press
{
	Shot before, during;
};

Press press(unsigned short scan, const juce::String &out, const juce::String &name)
{
	focus();
	std::printf("  %-10s foreground %s, focus %s, layout %s, scan 0x%02X types '%c'\n", name.toRawUTF8(), win::isForeground(g_hwnd) ? "yes" : "NO",
	            win::focusClass().c_str(), win::layoutName().c_str(), scan, char(win::typedBy(scan)));
	Press p;
	p.before = shoot(g_hwnd, out + "/" + name + "-before.png");
	win::key(scan, true);
	pump(500);
	p.during = shoot(g_hwnd, out + "/" + name + "-during.png");
	win::key(scan, false);
	pump(600);
	return p;
}

std::vector<bool> g_noise;

std::vector<bool> changed(const Press &p)
{
	std::vector<bool> m(size_t(p.during.w * p.during.h));
	for (size_t i = 0; i < m.size(); ++i)
		m[i] = pixelDiff(p.before, p.during, i) > 40 && !g_noise[i];
	return m;
}

juce::String boxText(const std::vector<bool> &m, int w)
{
	int n = 0, x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
	for (size_t i = 0; i < m.size(); ++i)
		if (m[i]) {
			const int x = int(i % size_t(w)), y = int(i / size_t(w));
			++n;
			x0 = std::min(x0, x);
			y0 = std::min(y0, y);
			x1 = std::max(x1, x);
			y1 = std::max(y1, y);
		}
	return n == 0 ? juce::String("nothing changed") : juce::String(n) + " px in " + juce::String(x0) + "," + juce::String(y0) + "-" + juce::String(x1) + "," + juce::String(y1);
}

double unlike(const Press &a, const Press &b, int *count)
{
	const auto ma = changed(a), mb = changed(b);
	int n = 0, d = 0;
	for (size_t i = 0; i < ma.size(); ++i)
		if (ma[i] || mb[i]) {
			++n;
			d += pixelDiff(a.during, b.during, i) > 40;
		}
	*count = n;
	return n == 0 ? 0.0 : double(d) / n;
}

} // namespace

int main(int argc, char **argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	if (argc < 6) {
		std::printf("usage: keyhost VST3 PLUGIN.vst3 OUTDIR CLICKX CLICKY\n");
		return 2;
	}
	juce::ScopedJuceInitialiser_GUI gui;
	const juce::String kind(argv[1]), target(argv[2]), out(argv[3]);
	juce::File(out).createDirectory();
	juce::AudioPluginFormatManager fm;
	juce::addDefaultFormatsToManager(fm);
	juce::AudioPluginFormat *format = nullptr;
	for (auto *f : fm.getFormats())
		if (f->getName() == kind)
			format = f;
	if (format == nullptr) {
		std::printf("no format %s\n", kind.toRawUTF8());
		return 2;
	}
	juce::OwnedArray<juce::PluginDescription> types;
	format->findAllTypesForFile(types, target);
	if (types.isEmpty()) {
		std::printf("no plugin at %s\n", target.toRawUTF8());
		return 2;
	}
	juce::String err;
	auto inst = fm.createPluginInstance(*types[0], 48000.0, 256, err);
	if (inst == nullptr) {
		std::printf("load failed: %s\n", err.toRawUTF8());
		return 2;
	}
	std::printf("%s %s %s\n", kind.toRawUTF8(), types[0]->name.toRawUTF8(), types[0]->version.toRawUTF8());
	inst->enableAllBuses();
	inst->prepareToPlay(48000.0, 256);

	struct Render : juce::Timer
	{
		juce::AudioPluginInstance &p;
		juce::AudioBuffer<float> buf;
		juce::MidiBuffer midi;
		explicit Render(juce::AudioPluginInstance &i)
		    : p(i), buf(std::max(2, std::max(i.getTotalNumInputChannels(), i.getTotalNumOutputChannels())), 256)
		{
		}
		void timerCallback() override
		{
			buf.clear();
			midi.clear();
			p.processBlock(buf, midi);
		}
	} render(*inst);
	render.startTimer(5);

	std::unique_ptr<juce::AudioProcessorEditor> ed(inst->createEditorIfNeeded());
	if (ed == nullptr) {
		std::printf("no editor\n");
		return 2;
	}
	juce::DocumentWindow w("keyhost", juce::Colours::black, 0, true);
	w.setUsingNativeTitleBar(true);
	w.setContentNonOwned(ed.get(), true);
	w.setTopLeftPosition(0, 0);
	w.setVisible(true);
	w.toFront(true);
	pump(3000);
	g_hwnd = w.getPeer()->getNativeHandle();
	const auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->totalArea;
	std::printf("editor %d x %d, window %d x %d, screen %d x %d\n", ed->getWidth(), ed->getHeight(), w.getWidth(), w.getHeight(), area.getWidth(), area.getHeight());

	// PrintWindow can come back black for GPU-drawn windows; then read the screen.
	const Shot loaded = shoot(g_hwnd, out + "/loaded.png");
	std::printf("PrintWindow shot level %.1f\n", meanLevel(loaded));
	if (meanLevel(loaded) < 2.0) {
		g_fromScreen = true;
		std::printf("using the screen, level %.1f\n", meanLevel(shoot(g_hwnd, out + "/loaded-screen.png")));
	}

	g_focusAt = ed->localPointToGlobal(juce::Point<int>(std::atoi(argv[4]), std::atoi(argv[5])));
	focus();
	std::vector<Shot> idle;
	for (int i = 0; i < 6; ++i) {
		idle.push_back(shoot(g_hwnd, out + "/idle-" + juce::String(i) + ".png"));
		pump(250);
	}
	g_noise.assign(size_t(idle[0].w * idle[0].h), false);
	for (size_t i = 1; i < idle.size(); ++i)
		for (size_t j = 0; j < g_noise.size(); ++j)
			if (pixelDiff(idle[0], idle[i], j) > 40)
				g_noise[j] = true;
	int noisy = 0;
	for (bool b : g_noise)
		noisy += b;
	std::printf("  %d px change on their own (left out)\n", noisy);

	const unsigned short kY = 0x15, kZ = 0x2C;
	win::restoreLayout();
	pump(300);
	const Press usY = press(kY, out, "us-Y");
	if (!win::selectGerman()) {
		std::printf("no German layout\nFAIL\n");
		return 1;
	}
	pump(300);
	const Press deY = press(kY, out, "german-Y");
	win::restoreLayout();
	pump(300);
	const Press usZ = press(kZ, out, "us-Z");
	win::selectGerman();
	pump(300);
	const Press deZ = press(kZ, out, "german-Z");
	win::restoreLayout();
	pump(300);

	for (const Press *p : {&usY, &deY, &usZ, &deZ})
		if (p->before.rgb.empty() || p->during.rgb.empty() || p->before.w != idle[0].w || p->during.w != idle[0].w) {
			std::printf("a window shot is missing\nFAIL\n");
			return 1;
		}
	const int pw = idle[0].w;
	std::printf("  changed by a press:\n");
	std::printf("    US     Y place: %s\n    German Y place: %s\n", boxText(changed(usY), pw).toRawUTF8(), boxText(changed(deY), pw).toRawUTF8());
	std::printf("    US     Z place: %s\n    German Z place: %s\n", boxText(changed(usZ), pw).toRawUTF8(), boxText(changed(deZ), pw).toRawUTF8());
	int nY = 0, nZ = 0, nYZ = 0;
	const double sameY = unlike(usY, deY, &nY), sameZ = unlike(usZ, deZ, &nZ), apart = unlike(usY, usZ, &nYZ);
	const bool tells = nYZ > 0 && apart > 0.3, okY = sameY < 0.05, okZ = sameZ < 0.05;
	std::printf("  German vs US, Y place: %.1f %% of %d px differ  %s\n", sameY * 100, nY, okY ? "ok" : "FAIL");
	std::printf("  German vs US, Z place: %.1f %% of %d px differ  %s\n", sameZ * 100, nZ, okZ ? "ok" : "FAIL");
	std::printf("  Y place vs Z place (US): %.1f %% of %d px differ  %s\n", apart * 100, nYZ, tells ? "ok (the check can tell)" : "FAIL (can't tell)");
	const int failures = int(!okY) + int(!okZ) + int(!tells);
	std::printf("%s\n", failures == 0 ? "PASS" : "FAIL");

	render.stopTimer();
	w.clearContentComponent();
	ed.reset();
	inst.reset();
	return failures == 0 ? 0 : 1;
}
