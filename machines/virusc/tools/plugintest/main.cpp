// Headless test of the machine's processor. Needs the ROM from the private folder.
#include "HardwareView.h"
#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <thread>
#include <cstdio>
#include <map>

namespace {
int g_failures = 0;

void check(bool ok, const juce::String &what)
{
	std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
	if (!ok)
		++g_failures;
}

float render(VirusCProcessor &p, int blocks, juce::MidiBuffer midi = {}, std::vector<float> *out = nullptr)
{
	float peak = 0;
	juce::AudioBuffer<float> buffer(2, 480);
	for (int b = 0; b < blocks; ++b) {
		buffer.clear();
		juce::MidiBuffer m;
		if (b == 0)
			m = midi;
		p.processBlock(buffer, m);
		peak = juce::jmax(peak, buffer.getMagnitude(0, 480));
		if (out != nullptr)
			out->insert(out->end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + 480);
	}
	return peak;
}

// Renders `samples` in blocks of `block`.
void renderBy(VirusCProcessor &p, int samples, int block)
{
	juce::AudioBuffer<float> buffer(2, block);
	for (int done = 0; done < samples; done += block) {
		buffer.clear();
		juce::MidiBuffer none;
		p.processBlock(buffer, none);
	}
}

juce::MidiBuffer noteOn(int note)
{
	juce::MidiBuffer m;
	m.addEvent(juce::MidiMessage::noteOn(1, note, uint8_t(100)), 0);
	return m;
}

juce::MidiBuffer noteOff(int note)
{
	juce::MidiBuffer m;
	m.addEvent(juce::MidiMessage::noteOff(1, note), 0);
	return m;
}
// A key held for 0.15 s, then 0.2 s to let the firmware act on it. In the STORE dialog the firmware
// can miss a 0.1 s tap (docs/firmware.md, "Panel keys").
void press(VirusCProcessor &p, int row, int bit)
{
	p.setKey(row, bit, true);
	render(p, 15);
	p.setKey(row, bit, false);
	render(p, 20);
}

juce::MidiBuffer program(int bank, int number)
{
	juce::MidiBuffer m;
	m.addEvent(juce::MidiMessage::controllerEvent(1, 32, bank), 0);
	m.addEvent(juce::MidiMessage::programChange(1, number), 1);
	return m;
}

// A parameter change as the Virus takes it over MIDI: page A $70, B $71, C $72; part 0-15, or $40
// for the single program.
juce::MidiBuffer sysex(int page, int part, int param, int value)
{
	const uint8_t data[] = {0xf0, 0x00, 0x20, 0x33, 0x01, 0x10, uint8_t(page), uint8_t(part), uint8_t(param), uint8_t(value), 0xf7};
	juce::MidiBuffer m;
	m.addEvent(data, int(sizeof(data)), 0);
	return m;
}

// The processor with OUT 2 and OUT 3 on, and the side-chain input on or off.
void setJacks(VirusCProcessor &p, bool input)
{
	auto layout = p.getBusesLayout();
	layout.outputBuses.getReference(1) = juce::AudioChannelSet::stereo();
	layout.outputBuses.getReference(2) = juce::AudioChannelSet::stereo();
	layout.inputBuses.getReference(0) = input ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
	p.setBusesLayout(layout);
}

// Renders `blocks` of 512 with every bus, the side-chain fed `inputHz` (0: silence) at -12 dB, or, with
// `stale`, the buffer's first two channels left holding that sine as a host may leave them with the
// side-chain off. Returns each of the six jacks' RMS over the last `measure` blocks, and the input's
// frequency's share of OUT 1 L.
struct Jacks {
	double rms[6]{};
	double toneOut1 = 0;
};
Jacks renderJacks(VirusCProcessor &p, int blocks, int measure, double inputHz, bool stale, juce::MidiBuffer first = {})
{
	Jacks j;
	const int channels = std::max(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
	juce::AudioBuffer<float> buffer(std::max(channels, 2), 512);
	double phase = 0, re = 0, im = 0, power = 0;
	for (int b = 0; b < blocks; ++b) {
		buffer.clear();
		if (inputHz > 0 && (stale || p.getTotalNumInputChannels() > 0))
			for (int s = 0; s < 512; ++s) {
				const float x = 0.25f * float(std::sin(phase + 2 * juce::MathConstants<double>::pi * inputHz * s / 48000.0));
				buffer.setSample(0, s, x);
				buffer.setSample(1, s, x);
			}
		phase += 2 * juce::MathConstants<double>::pi * inputHz * 512 / 48000.0;
		juce::MidiBuffer m;
		if (b == 0)
			m = first;
		p.processBlock(buffer, m);
		if (b < blocks - measure)
			continue;
		for (int jack = 0; jack < 6; ++jack) {
			const int ch = p.getChannelIndexInProcessBlockBuffer(false, jack / 2, jack % 2);
			for (int s = 0; s < 512; ++s)
				j.rms[jack] += double(buffer.getSample(ch, s)) * buffer.getSample(ch, s);
		}
		for (int s = 0; s < 512; ++s) {
			const double x = buffer.getSample(0, s), t = 2 * juce::MathConstants<double>::pi * inputHz * double(b * 512 + s) / 48000.0;
			re += x * std::cos(t);
			im += x * std::sin(t);
			power += x * x;
		}
	}
	for (auto &r : j.rms)
		r = std::sqrt(r / (measure * 512.0));
	const double n = measure * 512.0;
	j.toneOut1 = power > 0 ? (2 * (re * re + im * im) / n) / power : 0;
	return j;
}

juce::String db(double rms) { return rms > 0 ? juce::String(20 * std::log10(rms), 1) + " dB" : "-inf"; }

juce::String programName(VirusCProcessor &p)
{
	char lcd[2][17];
	p.lcdText(lcd);
	return juce::String(lcd[0] + 2, 14).trimEnd();
}
} // namespace

int main()
{
	juce::ScopedJuceInitialiser_GUI gui;
	juce::MemoryBlock rom;
	if (!juce::File(VIRUSC_ROM).loadFileAsData(rom)) {
		std::printf("No ROM at %s: skipped.\n", VIRUSC_ROM);
		return 0;
	}


	{
		// VC_ARTWORK: an artwork pack for the VC_SNAPSHOT picture.
		juce::MemoryBlock pack;
		if (const char *a = std::getenv("VC_ARTWORK"))
			juce::File(a).loadFileAsData(pack);
		VirusCProcessor p(&rom, lemus::Artwork::parse(&pack));
		p.prepareToPlay(48000.0, 480);
		p.waitForBoot();
		check(p.booted(), "the firmware boots to where it listens to MIDI");
		char lcd[2][17];
		render(p, 10);
		p.lcdText(lcd);
		check(lcd[0][2] == 'A' && lcd[0][3] == '0', "the LCD shows the first program: " + juce::String(lcd[0] + 2, 14));
		{
			// Read after every 64-sample block, as a host with small buffers does: a lit LED stays lit.
			// Idle on A0 nothing on the panel blinks (tools/viruscrun, VC_LEDWIN).
			float lo[virusc::Machine::kLedColumns][virusc::Machine::kLedLines], hi[virusc::Machine::kLedColumns][virusc::Machine::kLedLines];
			std::fill(&lo[0][0], &lo[0][0] + std::size(lo) * std::size(lo[0]), 1.0f);
			std::fill(&hi[0][0], &hi[0][0] + std::size(hi) * std::size(hi[0]), 0.0f);
			juce::AudioBuffer<float> small(2, 64);
			for (int b = 0; b < 750; ++b) {
				juce::MidiBuffer none;
				p.processBlock(small, none);
				float now[virusc::Machine::kLedColumns][virusc::Machine::kLedLines], lfo[2];
				p.ledState(now, lfo);
				for (int c = 0; c < virusc::Machine::kLedColumns; ++c)
					for (int l = 0; l < virusc::Machine::kLedLines; ++l) {
						lo[c][l] = std::min(lo[c][l], now[c][l]);
						hi[c][l] = std::max(hi[c][l], now[c][l]);
					}
			}
			int lit = 0, flicker = 0;
			for (int c = 0; c < virusc::Machine::kLedColumns; ++c)
				for (int l = 0; l < virusc::Machine::kLedLines; ++l) {
					lit += hi[c][l] > 0.5f;
					flicker += hi[c][l] > 0.5f && lo[c][l] < 0.3f;
				}
			check(lit > 10 && flicker == 0, juce::String("with 64-sample blocks the lit LEDs hold steady: ") + juce::String(flicker) +
			                                    " of " + juce::String(lit) + " flicker");
		}
		check(render(p, 100, noteOn(60)) > 0.01f, "a note sounds");
		{
			// The LFO RATE LEDs follow the LFOs, from the DSP's timers (A0's LFO 1 runs about 1.5 Hz).
			float lo = 1, hi = 0;
			for (int b = 0; b < 100; ++b) {
				render(p, 1);
				float now[virusc::Machine::kLedColumns][virusc::Machine::kLedLines], lfo[2];
				p.ledState(now, lfo);
				lo = std::min(lo, lfo[0]);
				hi = std::max(hi, lfo[0]);
			}
			check(hi > 0.8f && lo < 0.3f, juce::String("while it plays the LFO 1 RATE LED pulses: ") + juce::String(lo, 2) + " to " + juce::String(hi, 2));
		}
		{
			virusc::HardwareView view(p);
			render(p, 5);
			view.timerCallbackForTests();
			check(view.keyCount() == 35 && view.knobCount() == 32 && view.ledCount() == 69,
			      "the panel has its 35 keys, 32 knobs and 69 LEDs, each wired");
			if (const char *png = std::getenv("VC_SNAPSHOT")) {
				view.showRear(std::getenv("VC_SNAPSHOT_REAR") != nullptr); // the rear view instead
				const auto image = view.createComponentSnapshot(view.getLocalBounds(), true, 2.0f);
				juce::File(png).deleteFile();
				juce::FileOutputStream out{juce::File(png)};
				juce::PNGImageFormat().writeImageToStream(image, out);
			}
		}
		render(p, 100, noteOff(60));
		render(p, 1500);
		check(p.asleep(), "after the tail it sleeps");
		std::vector<float> asleep;
		render(p, 50, {}, &asleep);
		bool zeros = true;
		for (float v : asleep)
			zeros = zeros && v == 0.0f;
		check(zeros, "asleep, the output is exact zeros");
		{
			const auto t0 = juce::Time::getMillisecondCounterHiRes();
			render(p, 1000);
			const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
			std::printf("      asleep: 10 s rendered in %.0f ms, %.1f %% of a core\n", ms, ms / 100.0);
		}
		check(render(p, 100, noteOn(62)) > 0.01f && !p.asleep(), "a note wakes it");
		render(p, 100, noteOff(62));

		{
			// Past the end of bank B, asleep: PARAMETER > (row 3 bit 5), then VALUE+ and ARP EDIT still answer.
			render(p, 100, program(1, 127));
			render(p, 1500);
			const bool slept = p.asleep();
			const auto before = programName(p);
			press(p, 3, 5);
			const auto bankC = programName(p);
			press(p, 3, 6);
			const auto next = programName(p);
			press(p, 0, 0);
			char lcd[2][17];
			p.lcdText(lcd);
			for (char &c : lcd[0])
				if (c == 0)
					c = '#'; // the firmware's own symbol 0 would end the string
			const auto arp = juce::String(lcd[0], 16);
			if (std::getenv("VC_DEBUG"))
				std::printf("      asleep %d: %s -> %s -> %s -> %s\n", slept, before.toRawUTF8(), bankC.toRawUTF8(), next.toRawUTF8(), arp.toRawUTF8());
			check(bankC.startsWith("C") && next != bankC && arp.contains("ARPEGGIATOR"),
			      "past B127 PARAMETER > goes to bank C and the keys still answer: " + before + " / " + bankC + " / " + next + " / " + arp.trim());
			press(p, 0, 0); // leave the ARP page
		}

		// STORE, VALUE+ five times, STORE: A0 into A5 (MAME's matrix: STORE row 2 bit 4, VALUE+ row 3 bit 6)
		render(p, 100, program(0, 0));
		press(p, 2, 4);
		if (std::getenv("VC_DEBUG")) std::printf("      after STORE: %s\n", programName(p).toRawUTF8());
		for (int i = 0; i < 5; ++i) {
			press(p, 3, 6);
			if (std::getenv("VC_DEBUG")) std::printf("      after VALUE+: %s\n", programName(p).toRawUTF8());
		}
		press(p, 2, 4);
		if (std::getenv("VC_DEBUG")) std::printf("      after STORE: %s\n", programName(p).toRawUTF8());
		render(p, 250);
		render(p, 50, program(0, 5));
		check(programName(p) == "A5  AutoBendBC", "STORE puts A0 into A5: " + programName(p));
		render(p, 50, program(2, 5));
		check(programName(p) == "C5  B-SquareBC", "and leaves bank C, in flash, as it was: " + programName(p));

		juce::MemoryBlock state;
		p.getStateInformation(state);
		check(state.getSize() > 160000, "the state holds the RAM");

		VirusCProcessor q(&rom);
		q.prepareToPlay(48000.0, 480);
		q.setStateInformation(state.getData(), int(state.getSize()));
		q.waitForBoot();
		check(q.booted() && q.modelVersion() == VirusCProcessor::kModelVersion, "a session reloads, with its model version");
		check(render(q, 100, noteOn(60)) > 0.01f, "and plays");
		render(q, 50, program(0, 5));
		check(programName(q) == "A5  AutoBendBC", "the stored program is in the session: " + programName(q));

		// The rear panel's ON/OFF: off is silent and dark; on is a power-on, the battery's banks as they were
		q.setPowered(false);
		const float off = render(q, 20, noteOn(60));
		q.setPowered(true);
		q.waitForBoot();
		render(q, 100);
		render(q, 50, program(0, 5));
		check(off == 0.0f && q.booted() && programName(q) == "A5  AutoBendBC",
		      "switched off it is silent (" + juce::String(off) + "); switched on it boots with bank A as stored: " + programName(q));
	}

	{
		// In tune: A4 on C126 "- Init -" (a plain saw) at 48 kHz, against Osirus's 440.53 Hz for the same program and
		// note. The DSP once fell behind real time and repeated samples, A4 at 419.6 Hz here (docs/firmware.md,
		// "Running the DSP"). Then a six-note chord on E73 PowerStrnJ: the DSP hands over every frame.
		VirusCProcessor p(&rom);
		p.prepareToPlay(48000.0, 512);
		p.waitForBoot();
		renderBy(p, 48000, 512);
		std::vector<float> out;
		juce::MidiBuffer m = program(2, 126);
		juce::AudioBuffer<float> buffer(2, 512);
		for (int b = 0; b < 300; ++b) {
			buffer.clear();
			juce::MidiBuffer now;
			if (b == 0)
				now = m;
			if (b == 50)
				now.addEvent(juce::MidiMessage::noteOn(1, 69, uint8_t(100)), 0);
			p.processBlock(buffer, now);
			if (b >= 80 && b < 80 + 94)
				out.insert(out.end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + 512);
		}
		double best = 0, bestF = 0;
		for (double f = 430; f <= 450; f += 0.01) {
			double re = 0, im = 0;
			for (size_t i = 0; i < out.size(); ++i) {
				const double w = 0.5 - 0.5 * std::cos(2 * juce::MathConstants<double>::pi * double(i) / double(out.size()));
				re += w * out[i] * std::cos(2 * juce::MathConstants<double>::pi * f * double(i) / 48000.0);
				im += w * out[i] * std::sin(2 * juce::MathConstants<double>::pi * f * double(i) / 48000.0);
			}
			if (re * re + im * im > best) {
				best = re * re + im * im;
				bestF = f;
			}
		}
		const double cents = 1200 * std::log2(bestF / 440.53);
		check(std::abs(cents) < 3, "A4 on \"- Init -\" plays at " + juce::String(bestF, 2) + " Hz, " + juce::String(cents, 1) + " cents from Osirus");
		render(p, 50, program(4, 73));
		juce::MidiBuffer chord;
		for (int n : {50, 53, 57, 62, 65, 69})
			chord.addEvent(juce::MidiMessage::noteOn(1, n, uint8_t(100)), 0);
		const auto missed = p.machineForTests().framesMissed();
		render(p, 200, chord);
		check(p.machineForTests().framesMissed() == missed, "under a six-note chord the DSP hands over every frame: " +
		                                                         juce::String(int64_t(p.machineForTests().framesMissed() - missed)) + " missed");
	}

	{
		// The rear panel's jacks, as the firmware routes to them, on C126 "- Init -" with OUT 2 and OUT 3 on.
		// SECOND OUTPUT (page C 45: 0 off, then OUT 1 L ... AUX 2 R; 5 is OUT 2 L+R) at balance 127 (page B 58)
		// moves the single program to OUT 2; in MULTI mode (page C 122 = 1), part 0's OUTPUT SELECT (page C 41:
		// 7 is OUT 3 L+R) moves it to OUT 3. Then the side-chain: INPUT Static (page A 101 = 2) on IN L+R
		// (page A 102 = 1) plays a 1 kHz sine through the filters; with the side-chain off, the buffer's
		// first two channels hold that sine as Live leaves them, and nothing of it is heard.
		const auto fresh = [&rom](VirusCProcessor &p, bool input) {
			setJacks(p, input);
			p.prepareToPlay(48000.0, 512);
			p.waitForBoot();
			renderBy(p, 48000, 512);
			renderJacks(p, 20, 1, 0, false, program(2, 126));
		};
		const auto jacks = [](const Jacks &j) {
			juce::String s;
			for (double r : j.rms)
				s << " " << db(r);
			return s;
		};
		const auto only = [](const Jacks &j, int pair) {
			for (int jack = 0; jack < 6; ++jack)
				if ((jack / 2 == pair) != (j.rms[jack] > 0.01))
					return false;
			return true;
		};
		{
			VirusCProcessor p(&rom);
			fresh(p, false);
			renderJacks(p, 20, 1, 0, false, sysex(0x72, 0, 45, 5));
			renderJacks(p, 20, 1, 0, false, sysex(0x71, 0x40, 58, 127));
			const auto j = renderJacks(p, 60, 40, 0, false, noteOn(57));
			check(only(j, 1), "SECOND OUTPUT to OUT 2 L+R: the note is on OUT 2 only (OUT 1 L to OUT 3 R:" + jacks(j) + ")");
		}
		{
			VirusCProcessor p(&rom);
			fresh(p, false);
			renderJacks(p, 20, 1, 0, false, sysex(0x72, 0, 122, 1));
			renderJacks(p, 20, 1, 0, false, sysex(0x72, 0, 41, 7));
			const auto j = renderJacks(p, 60, 40, 0, false, noteOn(57));
			check(only(j, 2), "MULTI part 0 to OUT 3 L+R: the note is on OUT 3 only (" + jacks(j).trim() + ")");
		}
		for (bool on : {true, false}) {
			VirusCProcessor p(&rom);
			fresh(p, on);
			renderJacks(p, 20, 1, 0, false, sysex(0x70, 0x40, 101, 2));
			renderJacks(p, 20, 1, 0, false, sysex(0x70, 0x40, 102, 1));
			renderJacks(p, 100, 1, 0, false); // asleep by now: the input wakes it
			const auto j = renderJacks(p, 60, 40, 1000, !on);
			if (on)
				check(j.rms[0] > 0.01 && j.toneOut1 > 0.9, "INPUT Static on IN L+R: the side-chain's 1 kHz is on OUT 1 at " + db(j.rms[0]) + ", " +
				                                                 juce::String(100 * j.toneOut1, 1) + " % of it at 1 kHz");
			else
				// what is left is INPUT Static's own floor, about -98 dB
				check(j.rms[0] < 1e-4 && j.toneOut1 < 0.01, "with the side-chain off, what the host leaves in the buffer isn't heard: OUT 1 at " +
				                                                db(j.rms[0]) + ", " + juce::String(100 * j.toneOut1, 2) + " % of it at 1 kHz");
		}
	}

	if (const char *in = std::getenv("VC_SHELL_IN"); in != nullptr) {
		// Tool: VC_SHELL_IN, a Lemus plugin state (its ValueTree), comes back as VC_SHELL_OUT with the Virus
		// in it routed by VC_ROUTE: "second" (SECOND OUTPUT to OUT 2 L+R, balance 127), "multi" (MULTI,
		// part 0 to OUT 3 L+R) or "input" (C126 "- Init -" with INPUT Static on IN L+R). Prints where a note
		// goes after a reload of that state, or, for "input", where a 1 kHz side-chain goes.
		const juce::String route(std::getenv("VC_ROUTE"));
		const bool multi = route == "multi", input = route == "input";
		VirusCProcessor p(&rom);
		setJacks(p, input);
		p.prepareToPlay(48000.0, 512);
		p.waitForBoot();
		renderBy(p, 48000, 512);
		if (input) {
			renderJacks(p, 20, 1, 0, false, program(2, 126));
			renderJacks(p, 20, 1, 0, false, sysex(0x70, 0x40, 101, 2));
			renderJacks(p, 20, 1, 0, false, sysex(0x70, 0x40, 102, 1));
		} else if (multi) {
			renderJacks(p, 20, 1, 0, false, sysex(0x72, 0, 122, 1));
			renderJacks(p, 20, 1, 0, false, sysex(0x72, 0, 41, 7));
		} else {
			renderJacks(p, 20, 1, 0, false, sysex(0x72, 0, 45, 5));
			renderJacks(p, 20, 1, 0, false, sysex(0x71, 0x40, 58, 127));
		}
		renderJacks(p, 100, 1, 0, false);
		juce::MemoryBlock machine;
		p.getStateInformation(machine);
		VirusCProcessor q(&rom);
		setJacks(q, input);
		q.prepareToPlay(48000.0, 512);
		q.setStateInformation(machine.getData(), int(machine.getSize()));
		q.waitForBoot();
		renderBy(q, 48000, 512);
		const auto j = input ? renderJacks(q, 60, 40, 1000, false) : renderJacks(q, 60, 40, 0, false, noteOn(57));
		std::printf(input ? "after a reload, a 1 kHz side-chain:" : "after a reload, a note:");
		for (double r : j.rms)
			std::printf(" %s", db(r).toRawUTF8());
		std::printf("\n");
		juce::MemoryBlock shell;
		juce::File(in).loadFileAsData(shell);
		auto tree = juce::ValueTree::readFromData(shell.getData(), shell.getSize());
		std::printf("shell state: %s, machine %s\n", tree.getType().toString().toRawUTF8(), tree["machine"].toString().toRawUTF8());
		tree.setProperty("state", machine, nullptr);
		juce::MemoryOutputStream out;
		tree.writeToStream(out);
		juce::File(std::getenv("VC_SHELL_OUT")).replaceWithData(out.getData(), out.getDataSize());
		return 0;
	}

	{
		// The SYSTEM menu's STORE TO FLASH: RAM bank A over flash bank E. The firmware parks the 64 KB sector
		// (E and F) in the DSP, swaps the RAM in as code with P1.0 and runs its flash programmer from there
		// (docs/firmware.md, "Store to flash"). Bank A is a copy of bank C, so E0 becomes "AutoBendBC" and
		// F, in the same sector, stays as it was. The written sector is in the session. Before, the DSP
		// ran off into a bootstrap ROM the engine doesn't have and took the host down with it.
		VirusCProcessor p(&rom);
		setJacks(p, false);
		p.prepareToPlay(48000.0, 512);
		p.waitForBoot();
		renderBy(p, 48000, 512);
		juce::String page;
		for (int i = 0; i < 8 && page != "STORE TO FLASH"; ++i) {
			press(p, 1, 3); // SYSTEM: RANDOM, MIDI DUMP TX, MIDI, PURE TUNING, SYSTEM, STORE TO FLASH
			char lcd[2][17];
			p.lcdText(lcd);
			page = juce::String(lcd[0] + 2, 14).trimEnd();
		}
		press(p, 3, 6); // VALUE+: to bank D
		press(p, 3, 6); // and E
		char lcd[2][17];
		p.lcdText(lcd);
		const juce::String target = juce::String(lcd[1], 16).trimEnd();
		press(p, 2, 4); // STORE: "New ROM-Bank? [VAL+] execute!"
		press(p, 3, 6); // VALUE+
		juce::String result;
		for (int s = 0; s < 40 && !result.contains("OK"); ++s) {
			renderBy(p, 48000, 512);
			p.lcdText(lcd);
			result = juce::String(lcd[0], 16).trim() + " " + juce::String(lcd[1], 16).trim();
		}
		renderBy(p, 48000 * 15, 512); // the firmware starts again and boots the DSP
		render(p, 50, program(4, 0));
		const auto e0 = programName(p);
		render(p, 50, program(5, 0));
		const auto f0 = programName(p);
		render(p, 50, program(2, 126));
		const auto j = renderJacks(p, 60, 40, 0, false, noteOn(57));
		check(page == "STORE TO FLASH" && result.contains("OK!") && e0 == "E0  AutoBendBC" && f0 == "F0  7thHeavnJS" && j.rms[0] > 0.01,
		      "STORE TO FLASH (" + target + "): \"" + result + "\", then " + e0 + ", " + f0 + " as it was, C126 plays at " + db(j.rms[0]));
		juce::MemoryBlock state;
		p.getStateInformation(state);
		VirusCProcessor q(&rom);
		q.prepareToPlay(48000.0, 480);
		q.setStateInformation(state.getData(), int(state.getSize()));
		q.waitForBoot();
		render(q, 50, program(4, 0));
		check(programName(q) == "E0  AutoBendBC" && state.getSize() > 200000,
		      "the written sector is in the session: " + programName(q) + ", the state " + juce::String(int(state.getSize() / 1024)) + " KB");
	}

	{
		// Several instances: two processors on two threads at once render what one renders alone, sample for
		// sample (no state shared between them: the engine, the recompiler, the board).
		const auto play = [](VirusCProcessor &p, std::vector<float> &out) {
			p.prepareToPlay(48000.0, 512);
			p.waitForBoot();
			render(p, 20, program(2, 126));
			render(p, 300, noteOn(57), &out);
		};
		std::vector<float> alone, a, b;
		{
			VirusCProcessor p(&rom);
			play(p, alone);
		}
		{
			VirusCProcessor p(&rom), q(&rom);
			std::thread t([&] { play(q, b); });
			play(p, a);
			t.join();
		}
		check(!alone.empty() && a == alone && b == alone, "two instances at once on two threads render what one renders alone, sample for sample");
	}

	{
		// VALUE+ pressed 30 times, sleeping between, at 48 kHz in 512-sample blocks: before P3.2 carried
		// the host port's HREQ, the DSP's unasked words piled up, the firmware read them as answers to
		// its patch uploads, and at about the 20th press it waited for an answer for good.
		VirusCProcessor p(&rom);
		p.prepareToPlay(48000.0, 512);
		p.waitForBoot();
		renderBy(p, 48000, 512);
		int advanced = 0;
		auto last = programName(p);
		for (int i = 0; i < 30; ++i) {
			p.setKey(3, 6, true);
			renderBy(p, 7200, 512);
			p.setKey(3, 6, false);
			renderBy(p, 12000 + 2400 * (i % 7), 512);
			const auto now = programName(p);
			advanced += now != last;
			last = now;
		}
		check(advanced == 30, "VALUE+ steps through 30 programs: " + juce::String(advanced) + ", at " + last);
	}

	std::printf(g_failures ? "%d FAILED\n" : "All passed\n", g_failures);
	return g_failures ? 1 : 0;
}
