// keyhost: computer keys by place, end to end, in a built Lemus plugin (AU or VST3).
// Loads the plugin through JUCE's hosting, shows its editor in a window, renders audio on a timer
// (no audio device), clicks the panel for keyboard focus, then presses the keys at the US Y and
// US Z places, each in the layout in use (British here) and in German, as AppKit would deliver
// them: the key code of the place, the character the layout types there. Window shots before and
// during each press go to OUTDIR.
// Pass: for each place, the German press changes the window as the British press does; and the
// two places do different things (so the check can tell).
// usage: keyhost AudioUnit|VST3 IDENTIFIER-OR-PATH OUTDIR [CLICKX CLICKY]  (editor points)
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#import <AppKit/AppKit.h>
#include <csignal>
#include <vector>

extern "C" void saveLayout();
extern "C" bool selectGerman();
extern "C" void restoreLayout(bool final);
extern "C" void currentLayoutId(char *out, int n);
extern "C" unsigned short typedBy(unsigned short code);

static void finalRestore() { restoreLayout(true); }
static void onSignal(int) { restoreLayout(true); _exit(2); }

static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

struct Shot
{
	int w = 0, h = 0;
	std::vector<uint8_t> rgb;
};

static Shot shoot(NSWindow *win, const juce::String &path)
{
	const juce::String cmd = "/usr/sbin/screencapture -x -o -l" + juce::String((long)[win windowNumber]) + " '" + path + "'";
	system(cmd.toRawUTF8());
	Shot s;
	NSBitmapImageRep *rep = [NSBitmapImageRep imageRepWithContentsOfFile:[NSString stringWithUTF8String:path.toRawUTF8()]];
	if (rep == nil)
		return s;
	s.w = int([rep pixelsWide]);
	s.h = int([rep pixelsHigh]);
	s.rgb.resize(size_t(s.w * s.h * 3));
	const unsigned char *d = [rep bitmapData];
	const NSInteger spp = [rep samplesPerPixel], bpr = [rep bytesPerRow];
	for (int y = 0; y < s.h; ++y)
		for (int x = 0; x < s.w; ++x)
			for (int c = 0; c < 3; ++c)
				s.rgb[size_t((y * s.w + x) * 3 + c)] = d[y * bpr + x * spp + c];
	return s;
}

static int pixelDiff(const Shot &a, const Shot &b, size_t i)
{
	return std::abs(a.rgb[i * 3] - b.rgb[i * 3]) + std::abs(a.rgb[i * 3 + 1] - b.rgb[i * 3 + 1]) + std::abs(a.rgb[i * 3 + 2] - b.rgb[i * 3 + 2]);
}

static void sendKey(NSWindow *win, unsigned short code, unichar ch, bool down)
{
	NSString *chars = [NSString stringWithCharacters:&ch length:1];
	NSEvent *e = [NSEvent keyEventWithType:down ? NSEventTypeKeyDown : NSEventTypeKeyUp
	                              location:NSZeroPoint
	                         modifierFlags:0
	                             timestamp:[[NSProcessInfo processInfo] systemUptime]
	                          windowNumber:[win windowNumber]
	                               context:nil
	                            characters:chars
	           charactersIgnoringModifiers:chars
	                             isARepeat:NO
	                               keyCode:code];
	[win sendEvent:e];
}

static void click(NSWindow *win, NSPoint p)
{
	for (NSEventType t : {NSEventTypeLeftMouseDown, NSEventTypeLeftMouseUp}) {
		NSEvent *e = [NSEvent mouseEventWithType:t
		                                location:p
		                           modifierFlags:0
		                               timestamp:[[NSProcessInfo processInfo] systemUptime]
		                            windowNumber:[win windowNumber]
		                                 context:nil
		                             eventNumber:0
		                              clickCount:1
		                                pressure:t == NSEventTypeLeftMouseDown ? 1.0f : 0.0f];
		[win sendEvent:e];
		pump(100);
	}
}

struct Press
{
	Shot before, during;
	unichar typed = 0;
	char layout[128] = {};
};

static NSPoint g_focusAt;
static void focus(NSWindow *win)
{
	if ([NSApp activationPolicy] != NSApplicationActivationPolicyRegular)
		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
	[[NSRunningApplication currentApplication] activateWithOptions:NSApplicationActivateAllWindows];
	[NSApp activateIgnoringOtherApps:YES];
	[win makeKeyAndOrderFront:nil];
	pump(150);
	click(win, g_focusAt);
	pump(250);
}

static Press press(NSWindow *win, unsigned short code, const juce::String &out, const juce::String &name)
{
	focus(win);
	std::printf("  %-10s app active %s, policy %d, key window %s, first responder %s\n", name.toRawUTF8(), [NSApp isActive] ? "yes" : "NO", int([NSApp activationPolicy]), [win isKeyWindow] ? "yes" : "NO",
	            NSStringFromClass([[win firstResponder] class]).UTF8String);
	Press p;
	currentLayoutId(p.layout, sizeof p.layout);
	p.typed = typedBy(code);
	p.before = shoot(win, out + "/" + name + "-before.png");
	sendKey(win, code, p.typed, true);
	pump(500);
	p.during = shoot(win, out + "/" + name + "-during.png");
	sendKey(win, code, p.typed, false);
	pump(600);
	std::printf("  %-10s %s, key code %u types '%c'\n", name.toRawUTF8(), p.layout, code, char(p.typed));
	return p;
}

// Pixels a press changed: during vs before, summed RGB difference over 60.
static std::vector<bool> g_noise;
static std::vector<bool> changed(const Press &p)
{
	std::vector<bool> m(size_t(p.during.w * p.during.h));
	for (size_t i = 0; i < m.size(); ++i)
		m[i] = pixelDiff(p.before, p.during, i) > 40 && !g_noise[i];
	return m;
}

struct Box
{
	int n = 0, x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
};

static Box box(const std::vector<bool> &m, int w)
{
	Box b;
	for (size_t i = 0; i < m.size(); ++i)
		if (m[i]) {
			const int x = int(i % size_t(w)), y = int(i / size_t(w));
			++b.n;
			b.x0 = std::min(b.x0, x);
			b.y0 = std::min(b.y0, y);
			b.x1 = std::max(b.x1, x);
			b.y1 = std::max(b.y1, y);
		}
	return b;
}

static juce::String boxText(const Box &b)
{
	return b.n == 0 ? juce::String("nothing changed")
	                : juce::String(b.n) + " px in " + juce::String(b.x0) + "," + juce::String(b.y0) + "-" + juce::String(b.x1) + "," + juce::String(b.y1);
}

// Of the pixels either press changed, the share whose during-shots differ (summed RGB over 60).
static double unlike(const Press &a, const Press &b, int *count)
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

int main(int argc, char **argv)
{
	setvbuf(stdout, nullptr, _IOLBF, 0);
	if (argc < 4)
		return 1;
	juce::ScopedJuceInitialiser_GUI gui;
	[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
	[NSApp finishLaunching];
	saveLayout();
	std::atexit(finalRestore);
	std::signal(SIGINT, onSignal);
	std::signal(SIGTERM, onSignal);

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
		return 1;
	}
	juce::OwnedArray<juce::PluginDescription> types;
	format->findAllTypesForFile(types, target);
	if (types.isEmpty()) {
		std::printf("no plugin at %s\n", target.toRawUTF8());
		return 1;
	}
	juce::String err;
	auto inst = fm.createPluginInstance(*types[0], 48000.0, 256, err);
	if (inst == nullptr) {
		std::printf("load failed: %s\n", err.toRawUTF8());
		return 1;
	}
	std::printf("%s %s %s (%s)\n", kind.toRawUTF8(), types[0]->name.toRawUTF8(), types[0]->version.toRawUTF8(), types[0]->fileOrIdentifier.toRawUTF8());
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
		return 1;
	}
	juce::DocumentWindow w("keyhost", juce::Colours::black, 0, true);
	w.setUsingNativeTitleBar(true);
	w.setContentNonOwned(ed.get(), true);
	w.setTopLeftPosition(100, 100);
	w.setVisible(true);
	w.toFront(true);
	[NSApp activateIgnoringOtherApps:YES];
	pump(3000);
	NSView *content = (NSView *)w.getPeer()->getNativeHandle();
	NSWindow *win = [content window];
	shoot(win, out + "/loaded.png");
	std::printf("editor %d x %d\n", ed->getWidth(), ed->getHeight());

	int failures = 0;
	if (argc >= 6) {
		const auto at = ed->getLocalPoint(nullptr, juce::Point<int>(0, 0));
		juce::ignoreUnused(at);
		const NSPoint inView = NSMakePoint(atof(argv[4]), [content isFlipped] ? atof(argv[5]) : content.frame.size.height - atof(argv[5]));
		g_focusAt = [content convertPoint:inView toView:nil];
		focus(win);
		// What changes on its own (blinking LEDs): six idle shots 250 ms apart.
		std::vector<Shot> idle;
		for (int i = 0; i < 6; ++i) {
			idle.push_back(shoot(win, out + "/idle-" + juce::String(i) + ".png"));
			pump(250);
		}
		g_noise.assign(size_t(idle[0].w * idle[0].h), false);
		for (int i = 1; i < 6; ++i)
			for (size_t j = 0; j < g_noise.size(); ++j)
				if (pixelDiff(idle[0], idle[size_t(i)], j) > 40)
					g_noise[j] = true;
		int noisy = 0;
		for (bool b : g_noise)
			noisy += b;
		std::printf("  %d px change on their own (left out)\n", noisy);

		// Y place first in both layouts, then Z place (Z can change a mode, as the 101's octave).
		const unsigned short kY = 16, kZ = 6; // kVK_ANSI_Y, kVK_ANSI_Z
		restoreLayout(false);
		pump(300);
		const Press ukY = press(win, kY, out, "british-Y");
		if (!selectGerman()) {
			std::printf("no German layout\n");
			return 1;
		}
		pump(300);
		const Press deY = press(win, kY, out, "german-Y");
		restoreLayout(false);
		pump(300);
		const Press ukZ = press(win, kZ, out, "british-Z");
		selectGerman();
		pump(300);
		const Press deZ = press(win, kZ, out, "german-Z");
		restoreLayout(true);
		pump(300);

		for (const Press *p : {&ukY, &deY, &ukZ, &deZ})
			if (p->before.rgb.empty() || p->during.rgb.empty() || p->before.w != ukY.before.w || p->during.w != ukY.before.w) {
				std::printf("a window shot is missing\nFAIL\n");
				return 1;
			}
		const int pw = ukY.during.w;
		std::printf("  changed by a press:\n");
		std::printf("    British Y place: %s\n    German  Y place: %s\n", boxText(box(changed(ukY), pw)).toRawUTF8(), boxText(box(changed(deY), pw)).toRawUTF8());
		std::printf("    British Z place: %s\n    German  Z place: %s\n", boxText(box(changed(ukZ), pw)).toRawUTF8(), boxText(box(changed(deZ), pw)).toRawUTF8());
		int nY = 0, nZ = 0, nYZ = 0;
		const double sameY = unlike(ukY, deY, &nY), sameZ = unlike(ukZ, deZ, &nZ), apart = unlike(ukY, ukZ, &nYZ);
		const bool tells = nYZ > 0 && apart > 0.3;
		const bool okY = sameY < 0.05, okZ = sameZ < 0.05;
		std::printf("  German vs British, Y place: %.1f %% of %d px differ  %s\n", sameY * 100, nY, okY ? "ok" : "FAIL");
		std::printf("  German vs British, Z place: %.1f %% of %d px differ  %s\n", sameZ * 100, nZ, okZ ? "ok" : "FAIL");
		std::printf("  Y place vs Z place (British): %.1f %% of %d px differ  %s\n", apart * 100, nYZ, tells ? "ok (the check can tell)" : "FAIL (can't tell)");
		failures = int(!okY) + int(!okZ) + int(!tells);
		std::printf("%s\n", failures == 0 ? "PASS" : "FAIL");
	}
	render.stopTimer();
	w.clearContentComponent();
	ed.reset();
	inst.reset();
	return failures == 0 ? 0 : 1;
}
