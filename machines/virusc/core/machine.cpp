#include "machine.h"

#include "dsp56kEmu/disasm.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/dspBootCode.h"
#include "dsp56kEmu/jit.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"
#include "dsp56kBase/logging.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace virusc {

namespace {
constexpr uint32_t kDspMemory = 0x040000;      // P, X and Y (three SRAMs)
constexpr uint32_t kDspBridged = 0x020000;
// The DSP's idle loop in OS 6.6's program (Machine::idleStep): $2C087 jsr $2C091 / tst b / blt $2C087, the
// subroutine $2C091-$2C0A7 working out from r7 and Y:$2C082 and Y:$2C084 whether a block of frames is due
constexpr uint32_t kIdleHead = 0x2c089, kIdleFirst = 0x2c087, kIdleLast = 0x2c0a7; // the steps end at $2C089
constexpr uint32_t kIdleInputs[2] = {0x2c082, 0x2c084};
constexpr uint64_t kIdleSignature = 0x273206d6154a9bf3ull; // dspIdleLoopSignature with OS 6.6's program     // the address above which P, X and Y are one memory
constexpr double kMcuCyclesPerFrame = kMcuClock / 12.0 / kSampleRate;
constexpr uint64_t kMidiBitCycles = uint64_t(kMcuClock / 12.0 / 31250.0 + 0.5);

enum { ICR_RREQ = 0x01, ICR_TREQ = 0x02, ICR_HF0 = 0x08, ICR_HF1 = 0x10, ICR_HLEND = 0x20, ICR_INIT = 0x80 };
enum { ISR_RXDF = 0x01, ISR_TXDE = 0x02, ISR_TRDY = 0x04, ISR_HF2 = 0x08, ISR_HF3 = 0x10, ISR_HREQ = 0x80 };
enum { CVR_HC = 0x80 };
} // namespace

// The engine, in the arrangement Gearmulator gives it for this board (GPL-3.0; README, "Code
// from other projects"): 256 KB words of external memory, the 56362's peripherals in X I/O.
struct Machine::Dsp {
	Dsp()
	{
		static const bool quiet = [] {
			if (std::getenv("VIRUSC_DSP_LOG") == nullptr)
				Logging::setLogFunc([](const std::string &) {});
			return true;
		}();
		(void)quiet;
		const size_t needed = dsp56k::alignedSize<dsp56k::DSP>() + dsp56k::alignedSize<dsp56k::Memory>() +
		                      dsp56k::Memory::calcMemSize(kDspMemory, kDspBridged) * sizeof(uint32_t);
		buffer.resize(dsp56k::alignedSize(needed));
		auto *b = dsp56k::alignedAddress(buffer.data());
		auto *memClass = b + dsp56k::alignedSize<dsp56k::DSP>();
		auto *memSpace = memClass + dsp56k::alignedSize<dsp56k::Memory>();
		memory = new (memClass) dsp56k::Memory(validator, kDspMemory, kDspMemory, kDspBridged, reinterpret_cast<dsp56k::TWord *>(memSpace));
		dsp = new (b) dsp56k::DSP(*memory, &periphX, &periphY);
		auto conf = dsp->getJit().getConfig();
		conf.aguSupportBitreverse = false;
		dsp->getJit().setConfig(conf);
		boot = std::make_unique<dsp56k::DspBoot>(*dsp);
		periphX.getHDI08().setTransmitDataAlwaysEmpty(false);
	}
	~Dsp()
	{
		boot.reset();
		dsp->~DSP();
		memory->~Memory();
	}

	dsp56k::HDI08 &hdi() { return periphX.getHDI08(); }
	dsp56k::Esai &esai() { return periphX.getEsai(); }

	dsp56k::DefaultMemoryValidator validator;
	dsp56k::Peripherals56362 periphX;
	dsp56k::PeripheralsNop periphY;
	std::vector<uint8_t> buffer;
	dsp56k::Memory *memory = nullptr;
	dsp56k::DSP *dsp = nullptr;
	std::unique_ptr<dsp56k::DspBoot> boot;
	bool started = false;
	std::vector<uint32_t> bootHeader; // the bootstrap ROM's length and address, until both have come
	bool bootLoading = false;         // a header was taken: the words now go to the program
	// the idle loop (Machine::idleStep): whether it is OS 6.6's (-1: not looked yet), and the state, counters
	// and inputs at the loop's head the last time round
	int idleKnown = -1;
	bool idleArmed = false;
	std::array<uint64_t, 8> idleInputs{};
	uint64_t idleInstructions = 0, idleCycles = 0;
};

Machine::Machine() : m_ram(5 * 0x8000), m_dsp(std::make_unique<Dsp>())
{
	m_idleSkip = std::getenv("VIRUSC_NO_IDLE_SKIP") == nullptr; // for comparing with the loop run in full
	m_mcu.setBus(this);
	m_rows.fill(0xff);
	m_knobs.fill(64);
	m_knobs[2] = 127; // MASTER VOLUME, up
}

Machine::~Machine() = default;

bool Machine::setRom(const uint8_t *rom, size_t size)
{
	if (rom == nullptr || size != size_t(kRomSize))
		return false;
	m_rom.assign(rom, rom + size);
	m_sectorWritten.fill(false);
	// The battery RAM as a unit leaves the factory: the multis and RAM banks A and B, which the
	// firmware reads through the data window at banks 9, 10 and 11, hold what the flash has at those
	// banks (docs/firmware.md). The rest is clear.
	std::fill(m_ram.begin(), m_ram.end(), uint8_t(0));
	for (int bank = 9; bank <= 11; ++bank)
		std::copy_n(m_rom.begin() + bank * 0x8000, 0x8000, m_ram.begin() + 0x8000 * (1 + (bank & 3)));
	return true;
}

void Machine::powerOn()
{
	m_dsp = std::make_unique<Dsp>();
	m_lcd.reset();
	m_p3Latch = 0xff; // the port's pins come up high
	m_icr = 0;
	m_cvr = 0x32;
	m_rxValid = false;
	m_hostWords = 0;
	m_romBank = 3; // MAME's machine_start; the firmware selects its own before it uses the window
	m_ramBank = 0;
	m_swapped = false;
	m_server = false;
	m_flashCycle = 0;
	m_flashProgram = m_flashEraseSetup = m_flashId = false;
	m_mcu.setCode(m_rom.data(), m_rom.data() + m_romBank * 0x8000);
	m_mcu.reset();
	m_mcuDebt = 0.0;
	m_frames = 0;
	std::fill(std::begin(m_out), std::end(m_out), 0.0f);
	m_rxBusy = false;
	m_midiIn.clear();
	m_midiOut.clear();
	auto &in = m_dsp->esai().getAudioInputs();
	for (int i = 0; i < kInputAhead; ++i)
		in.push_back({});
}

void Machine::runMcuShare()
{
	hostPins(); // the DSP may have sent a word since
	m_mcuDebt += kMcuCyclesPerFrame;
	if (const int n = int(m_mcuDebt); n > 0) {
		const int ran = m_mcu.run(n);
		m_mcuDebt -= ran;
	}
	m_lcd.advance(uint64_t(270000.0 / kSampleRate + 0.5));
}

void Machine::runFrameDspHeld()
{
	runMcuShare();
	std::fill(std::begin(m_out), std::end(m_out), 0.0f);
	++m_frames;
}

void Machine::runFrame()
{
	runMcuShare();
	hostService();

	if (m_dsp->started) {
		auto &out = m_dsp->esai().getAudioOutputs();
		auto &in = m_dsp->esai().getAudioInputs();
		auto &dsp = *m_dsp->dsp;
		// The DSP runs until the ESAI hands over the next frame, two slots of getCyclesPerSample() instructions
		// each. The engine's frame clock waits while the DSP hasn't taken an interrupt request, so in a stretch
		// with interrupts masked (a patch change) a frame comes several frame periods late and the ones behind it
		// back to back. The budget covers that: with two slots, the DSP fell behind real time under load, the
		// frames it hadn't made repeated the last sample, and everything played flat (E73's A4 a semitone low,
		// docs/firmware.md, "Running the DSP"). Before the ESAI sends anything, one frame's worth is enough.
		const uint64_t slots = m_framesMade ? kDspBudgetFrames * 2u : 2u;
		const uint64_t limit = dsp.getInstructionCounter() + slots * m_dsp->periphX.getEsaiClock().getCyclesPerSample();
		while (out.empty() && dsp.getInstructionCounter() < limit) {
			if (onDspBlock) {
				const uint32_t pc = dsp.getPC().toWord();
				const uint64_t before = dsp.getInstructionCounter();
				dsp.exec();
				onDspBlock(pc, dsp.getInstructionCounter() - before);
			} else
				dsp.exec();
			if (m_idleSkip)
				idleStep(limit);
			if (dsp.getPC().toWord() >= m_dsp->memory->sizeP()) {
				dspToBootRom();
				std::fill(std::begin(m_out), std::end(m_out), 0.0f);
				++m_frames;
				return;
			}
		}
		if (out.empty() && m_framesMade)
			++m_framesMissed;
		if (!out.empty()) {
			++m_framesMade;
			const auto frame = out.pop_front();
			// the input frame the DSP reads kInputAhead frames on: IN L and IN R on receiver 0
			dsp56k::Audio::RxFrame rx;
			rx.resize(2);
			rx[0] = {dsp56k::sample2dsp<float>(m_inL), 0, 0, 0};
			rx[1] = {dsp56k::sample2dsp<float>(m_inR), 0, 0, 0};
			in.push_back(rx);
			if (frame.size() >= 2)
				for (int tx = 0; tx < kOutputs / 2; ++tx) {
					m_out[2 * tx] = dsp56k::dsp2sample<float>(frame[0][size_t(tx)]);
					m_out[2 * tx + 1] = dsp56k::dsp2sample<float>(frame[1][size_t(tx)]);
				}
		}
	}
	++m_frames;
}

// A word for the DSP's bootstrap ROM, which loads a program over the host port: its length, the address
// it goes to, then the words (DSP56362 user manual, appendix A). A length of 0 loads nothing and jumps
// to the address at once (a DO loop with a count of 0 doesn't run): to $1000xx, the program that holds
// the flash data (serverWord). The ROM takes any header; here one that doesn't fit in P memory is
// dropped a word at a time, so what the firmware writes over the host port's addresses while it starts
// again (its RAM test) doesn't keep the DSP from booting.
void Machine::bootWord(uint32_t word)
{
	auto &d = *m_dsp;
	if (d.bootHeader.size() < 2 && !d.bootLoading) {
		d.bootHeader.push_back(word);
		if (d.bootHeader.size() < 2)
			return;
		const uint32_t length = d.bootHeader[0], address = d.bootHeader[1], size = d.memory->sizeP();
		if (length == 0 && (address >> 8) == 0x1000) {
			// the DSP jumps to the program that holds the flash data (serverWord)
			d.bootHeader.clear();
			m_server = true;
			m_serverData.assign(1, uint8_t(address));
			m_serverRead = 0;
			return;
		}
		if (address < size && length <= size - address) {
			if (length == 0) {
				d.dsp->setPC(address);
				d.started = true;
				d.bootHeader.clear();
				return;
			}
			d.boot->hdiWriteTX(length);
			d.boot->hdiWriteTX(address);
			d.bootHeader.clear();
			d.bootLoading = true;
			return;
		}
		d.bootHeader.erase(d.bootHeader.begin());
		return;
	}
	if (d.boot->hdiWriteTX(word)) {
		d.dsp->setPC(d.boot->getInitialPC());
		d.started = true;
	}
}

// The DSP's idle loop. Between blocks of frames the program goes round $2C087-$2C0A7, working out from r7
// (which the audio interrupt moves) and two words in Y memory whether the next block is due; with one note
// on "- Init -" that was 89 % of its instructions, with an eight-note pad 64 %. Once a pass through it
// changed nothing (what it reads as it was the time before) and no interrupt waits,
// every pass up to the next time the peripherals run would change nothing either: no interrupt can come
// before then, and the 80C515 doesn't run meanwhile. So the board counts those passes as run (the engine's
// fastForward, by the instructions and cycles one took), up to the first pass's end at or past that time.
// That is where the run would have them run: no instruction in the loop touches a peripheral, so between
// the steps that end at $2C089 is the only place they do. Only with OS 6.6's code at those addresses
// (kIdleSignature). Bit-identical to running them (the golden renders, docs/firmware.md, "Running the DSP").
void Machine::idleStep(uint64_t limit)
{
	auto &d = *m_dsp;
	auto &dsp = *d.dsp;
	const uint32_t pc = dsp.getPC().toWord();
	if (pc < kIdleFirst || pc > kIdleLast) {
		d.idleArmed = false;
		return;
	}
	if (pc != kIdleHead)
		return;
	if (d.idleKnown < 0)
		d.idleKnown = dspIdleLoopSignature() == kIdleSignature ? 1 : 0;
	if (d.idleKnown == 0)
		return;
	// What a pass reads: r7, the two words, the status and mode registers, the stack it calls through. What it
	// writes (a, b, x0, x1, y1, the condition codes, Y:$2C084, the stack's entry) follows from those, so with
	// these the same at two heads in a row, the pass between them changed nothing.
	const auto &regs = dsp.regs();
	const std::array<uint64_t, 8> inputs{uint64_t(regs.r[7].var), d.memory->get(dsp56k::MemArea_Y, kIdleInputs[0]),
	                                     d.memory->get(dsp56k::MemArea_Y, kIdleInputs[1]), uint64_t(regs.sr.var),
	                                     uint64_t(regs.omr.var), uint64_t(regs.sp.var), uint64_t(regs.sc.var),
	                                     uint64_t(regs.m[7].var)};
	uint64_t instructions = dsp.getInstructionCounter(), cycles = dsp.getCycles();
	if (d.idleArmed && inputs == d.idleInputs && !dsp.hasPendingInterrupts()) {
		const uint64_t pass = instructions - d.idleInstructions, passCycles = cycles - d.idleCycles;
		const uint64_t until = std::min<uint64_t>(dsp.getPeriph(0)->getTargetClock(), limit);
		if (pass > 0 && until > instructions + pass) {
			// to the first head at or past it, where the run would have the peripherals run or end the frame
			const uint64_t passes = (until - instructions + pass - 1) / pass;
			dsp.fastForward(dsp56k::TWord(passes * pass), dsp56k::TWord(passes * passCycles));
			instructions += passes * pass;
			cycles += passes * passCycles;
			m_idleSkipped += passes * pass;
		}
	}
	d.idleInputs = inputs;
	d.idleInstructions = instructions;
	d.idleCycles = cycles;
	d.idleArmed = true;
}

// Stand-in for the DSP program at P:$1000xx that holds the flash data while the firmware writes the
// flash ("Store to flash"). It isn't in what this board has, so it is made from what the firmware
// does with it: the word that starts it ($1000xx) and every word $10 00 yy after that add a byte (xx,
// then yy); $000000 goes back to the first; a word whose last byte is $20 asks for the next one, which
// comes back in the receive registers; $606060, the first thing the firmware sends when it starts
// again, ends it, and the DSP waits in its bootstrap ROM.
void Machine::serverWord(uint32_t word, uint8_t last)
{
	if (word == 0x606060) {
		// back to the bootstrap ROM, through RESET as from $F456: the host port starts over too
		m_server = false;
		m_serverData.clear();
		m_dsp->bootHeader.clear();
		hostReset();
		return;
	}
	if (word == 0) {
		m_serverRead = 0;
		return;
	}
	if (last == 0x20) {
		const uint32_t b = m_serverRead < m_serverData.size() ? m_serverData[m_serverRead++] : 0xff;
		m_rxWord = (b << 16) | b; // read as the first byte with HLEND set or clear
		m_rxValid = true;
		return;
	}
	if ((word >> 16) == 0x10 && m_serverData.size() < 0x20000)
		m_serverData.push_back(uint8_t(word));
}

// The flash, an AMD Am29F040B: 512 KB in eight 64 KB sectors. The 80C515 writes it only while P1.0 is
// low: commands are unlocked by $AA at $5555 and $55 at $2AAA; then $A0 programs the next byte written
// (bits only go from 1 to 0), $80 and a second unlock then $30 at a sector erases it to $FF ($10 at
// $5555 the whole chip), $90 shows the maker's and the device's codes, $F0 returns to reading. A
// program answers the next read with its status, as the chip does while it works (DQ7 the inverse of
// the byte's bit 7, DQ6 toggling), and is done after it: the firmware polls the bank's last byte, not
// the one it wrote, and takes a set DQ7 for done. Erases are done at once.
uint32_t Machine::flashAddress(uint16_t a) const
{
	return a < 0x8000 ? a : uint32_t(m_romBank) * 0x8000 + (a - 0x8000u);
}

uint8_t Machine::flashRead(uint32_t address)
{
	if (m_flashBusy) {
		m_flashBusy = false;
		m_flashToggle ^= 0x40;
		return uint8_t((~m_flashLast & 0x80) | m_flashToggle);
	}
	if (m_flashId)
		return (address & 3) == 0 ? 0x01 : (address & 3) == 1 ? 0xa4 : 0x00;
	return m_rom[address & (kRomSize - 1)];
}

void Machine::flashWrite(uint32_t address, uint8_t d)
{
	address &= kRomSize - 1;
	const uint32_t low = address & 0x7fff;
	if (d == 0xf0) {
		m_flashCycle = 0;
		m_flashProgram = m_flashEraseSetup = m_flashId = false;
		return;
	}
	if (m_flashProgram) {
		m_flashProgram = false;
		m_flashBusy = true;
		m_flashLast = d;
		m_rom[address] &= d;
		m_sectorWritten[address >> 16] = true;
		return;
	}
	switch (m_flashCycle) {
	case 0:
		m_flashCycle = (low == 0x5555 && d == 0xaa) ? 1 : 0;
		return;
	case 1:
		m_flashCycle = (low == 0x2aaa && d == 0x55) ? 2 : 0;
		return;
	default:
		m_flashCycle = 0;
		if (m_flashEraseSetup) {
			m_flashEraseSetup = false;
			if (d == 0x30) {
				std::fill_n(m_rom.begin() + (address & ~0xffffu), 0x10000, uint8_t(0xff));
				m_sectorWritten[address >> 16] = true;
			} else if (d == 0x10 && low == 0x5555) {
				std::fill(m_rom.begin(), m_rom.end(), uint8_t(0xff));
				m_sectorWritten.fill(true);
			}
			return;
		}
		if (low != 0x5555)
			return;
		if (d == 0xa0)
			m_flashProgram = true;
		else if (d == 0x80)
			m_flashEraseSetup = true;
		else if (d == 0x90)
			m_flashId = true;
		return;
	}
}

std::vector<uint8_t> Machine::flashChanges() const
{
	std::vector<uint8_t> out;
	for (int sector = 0; sector < 8; ++sector)
		if (m_sectorWritten[size_t(sector)]) {
			out.push_back(uint8_t(sector));
			out.insert(out.end(), m_rom.begin() + sector * 0x10000, m_rom.begin() + (sector + 1) * 0x10000);
		}
	return out;
}

void Machine::setFlashChanges(const std::vector<uint8_t> &changes)
{
	for (size_t at = 0; at + 1 + 0x10000 <= changes.size() && m_rom.size() == size_t(kRomSize); at += 1 + 0x10000) {
		const int sector = changes[at] & 7;
		std::copy_n(changes.begin() + long(at) + 1, 0x10000, m_rom.begin() + sector * 0x10000);
		m_sectorWritten[size_t(sector)] = true;
	}
}

// RESET resets the host port, its host side too (DSP56362 user manual, the HI08's reset): no byte order
// swapped, no host command, the vector as at power-on. The firmware relies on it: after the flash is
// written it boots the DSP again without setting the byte order, which its other routines leave swapped.
void Machine::hostReset()
{
	m_icr = 0;
	m_cvr = 0x32;
	m_ivr = 0x0f;
	m_rxValid = false;
}

// The DSP's program jumped to its bootstrap ROM (P:$FF0000), after setting OMR to boot from the host
// port and RESET: the firmware asks for that to write the flash (command $F456, "Store to flash"). The
// engine doesn't have the ROM, so the board stands in for it: a DSP as at power-on, waiting for a
// program over the host port (bootWord). Its memory starts clear, where the unit's keeps what the old
// program left; the program loaded over it sets up what it uses.
void Machine::dspToBootRom()
{
	m_dsp = std::make_unique<Dsp>();
	hostReset();
	auto &in = m_dsp->esai().getAudioInputs();
	for (int i = 0; i < kInputAhead; ++i)
		in.push_back({});
	++m_dspReboots;
}

void Machine::warmUp()
{
	const int chord[4] = {48, 55, 62, 69};
	const auto run = [this](double seconds) {
		for (int i = 0; i < int(seconds * kSampleRate); ++i)
			runFrame();
	};
	for (int n : chord)
		for (int d : {0x90, n, 100})
			midiIn(uint8_t(d));
	run(0.25);
	for (int n : chord)
		for (int d : {0x80, n, 0})
			midiIn(uint8_t(d));
	const int quietFrames = int(kSampleRate / 10), limit = int(4.0 * kSampleRate);
	int quiet = 0;
	for (int i = 0; i < limit && quiet < quietFrames; ++i) {
		runFrame();
		quiet = std::all_of(std::begin(m_out), std::end(m_out), [](float x) { return std::abs(x) < 1e-6f; }) ? quiet + 1 : 0;
	}
	m_midiOut.clear();
}

std::vector<uint8_t> Machine::takeMidiOut()
{
	std::vector<uint8_t> out;
	out.swap(m_midiOut);
	return out;
}

void Machine::setKey(int row, int bit, bool down)
{
	auto &r = m_rows[size_t(row & 7)];
	r = uint8_t(down ? r & ~(1 << bit) : r | (1 << bit));
}

// The time since the last port change goes to the LEDs that were lit for it.
void Machine::ledAccount()
{
	const uint64_t now = m_mcu.cycles();
	const uint64_t dt = now - m_ledSince;
	m_ledSince = now;
	if (dt == 0)
		return;
	const int c = m_ledColumn;
	m_colTime[c] += dt;
	for (int l = 0; l < 7; ++l) {
		if ((m_latchA >> l) & 1)
			m_ledOn[c][l] += dt;
		if ((m_latchB >> l) & 1)
			m_ledOn[c][7 + l] += dt;
	}
}

void Machine::ledDuty(float out[kLedColumns][kLedLines])
{
	ledAccount();
	for (int c = 0; c < kLedColumns; ++c) {
		if (m_colTime[c] >= kLedWindow) {
			for (int l = 0; l < kLedLines; ++l) {
				m_ledOut[c][l] = float(double(m_ledOn[c][l]) / double(m_colTime[c]));
				m_ledOn[c][l] = 0;
			}
			m_colTime[c] = 0;
		}
		for (int l = 0; l < kLedLines; ++l)
			out[c][l] = m_ledOut[c][l];
	}
	m_colTime[7] = 0; // no column selected
	for (auto &v : m_ledOn[7])
		v = 0;
}

void Machine::dspTimer(int index, uint32_t &tcsr, uint32_t &tlr, uint32_t &tcpr) const
{
	const auto &t = m_dsp->periphX.getTimers();
	tcsr = t.readTCSR(index);
	tlr = t.readTLR(index);
	tcpr = t.readTCPR(index);
}

// Timers, reading and polarity from Gearmulator's virusLib (GPL-3.0; README, "Code from other projects").
float Machine::lfoLed(int which) const
{
	uint32_t tcsr, tlr, tcpr;
	dspTimer(which == 0 ? 2 : 1, tcsr, tlr, tcpr);
	if (!(tcsr & 1) || tlr >= 0xffffff) // timer off
		return 0.0f;
	// PWM: the counter runs from the load value to $FFFFFF; the pin changes at the compare value.
	const float high = float(std::min(std::max(tcpr, tlr), 0xffffffu) - tlr) / float(0xffffff - tlr);
	const float duty = 1.0f - high; // lit while the pin is low, as Gearmulator reads it
	return std::min(1.0f, duty * float(kLedColumns));
}

bool Machine::dspStarted() const { return m_dsp->started; }
uint32_t Machine::dspPc() const { return m_dsp->dsp->getPC().var; }
uint32_t Machine::dspX(uint32_t address) const { return m_dsp->memory->get(dsp56k::MemArea_X, address); }

uint64_t Machine::dspIdleLoopSignature() const
{
	uint64_t h = 1469598103934665603ull; // FNV-1a over the loop's program words
	for (uint32_t a = kIdleFirst; a <= kIdleLast; ++a) {
		h ^= m_dsp->memory->get(dsp56k::MemArea_P, a);
		h *= 1099511628211ull;
	}
	return h;
}
uint64_t Machine::dspInstructions() const { return m_dsp->dsp->getInstructionCounter(); }
size_t Machine::framesQueued() const { return m_dsp->esai().getAudioOutputs().size(); }
size_t Machine::dspRxPending() const { return m_dsp->hdi().rxData().size(); }
size_t Machine::dspTxPending() const { return m_dsp->hdi().txData().size(); }

std::string Machine::dspDisassemble(uint32_t from, int count) const
{
	auto &dsp = *m_dsp->dsp;
	dsp56k::Disassembler dis(dsp.opcodes());
	std::string out;
	uint32_t pc = from;
	for (int i = 0; i < count; ++i) {
		std::string line;
		const auto op = m_dsp->memory->get(dsp56k::MemArea_P, pc), opB = m_dsp->memory->get(dsp56k::MemArea_P, pc + 1);
		const uint32_t len = std::max(1u, dis.disassemble(line, op, opB, dsp.getSR().var, dsp.readRegs().omr.var, pc));
		char head[32];
		std::snprintf(head, sizeof(head), "P:%06x  ", pc);
		out += head + line + "\n";
		pc += len;
	}
	return out;
}

uint32_t Machine::dspPctl() const { return m_dsp->periphX.getEsaiClock().getPCTL(); }

// ---------------------------------------------------------------- the 80C515's bus

uint8_t Machine::xread(uint16_t a)
{
	if (a >= 0x0400 && a <= 0x0407)
		++m_hostBySelect[(m_p3Latch >> 3) & 1];
	if (a >= 0x0400 && a <= 0x0407 && hostSelected()) {
		const uint8_t v = hostRead(a & 7);
		if (m_hostLogOn && m_hostLog.size() < 200000)
			m_hostLog.push_back({m_mcu.cycles(), m_mcu.pc(), 0, uint8_t(a & 7), v});
		return v;
	}
	if (m_swapped)
		return flashRead(flashAddress(a));
	if (a < 0x8000)
		return m_ram[a];
	if (onXRead)
		onXRead(m_mcu.pc(), a, m_p5Latch);
	return m_ram[size_t(0x8000 * (1 + m_ramBank)) + (a - 0x8000)];
}

void Machine::xwrite(uint16_t a, uint8_t d)
{
	if (onXWrite)
		onXWrite(m_mcu.pc(), a, d, m_p5Latch);
	if (a >= 0x0400 && a <= 0x0407)
		++m_hostBySelect[2 + ((m_p3Latch >> 3) & 1)];
	if (a >= 0x0400 && a <= 0x0407 && hostSelected()) {
		if (m_hostLogOn && m_hostLog.size() < 200000)
			m_hostLog.push_back({m_mcu.cycles(), m_mcu.pc(), 1, uint8_t(a & 7), d});
		hostWrite(a & 7, d);
		return;
	}
	if (m_swapped) {
		flashWrite(flashAddress(a), d);
		return;
	}
	if (a < 0x8000)
		m_ram[a] = d;
	else
		m_ram[size_t(0x8000 * (1 + m_ramBank)) + (a - 0x8000)] = d;
}

uint8_t Machine::portIn(int port)
{
	if (onPortIn)
		onPortIn(port);
	switch (port) {
	case 1: // the LCD's data lines while it drives them
		return uint8_t((m_lcdRead >> 3) & 0x1e) | 0xe1;
	case 3: // P3.2: the host port's HREQ, low while it asks to be served (hostPins)
		return m_hreq ? 0xfb : 0xff;
	case 4:
		return m_rows[size_t(m_scan & 7)];
	default:
		return 0xff;
	}
}

void Machine::selectBanks(uint8_t p5)
{
	if (onP5 && p5 != m_p5Latch)
		onP5(p5);
	m_romBank = (p5 >> 4) & 15;
	m_mcu.setCodeBank(m_rom.data() + m_romBank * 0x8000);
	m_ramBank = (p5 >> 4) & 3; // the same bits, for the battery RAM in the data window (docs/firmware.md)
	ledAccount();
	m_scan = p5 & 15;
	m_ledColumn = p5 & 7;
	if (p5 & 8) // the second LED latch is open while P5 bit 3 is high
		m_latchB = m_p4Latch & 0x7f;
	m_p5Latch = p5;
}

void Machine::portOut(int port, uint8_t d)
{
	if (onPortOut)
		onPortOut(port, d);
	switch (port) {
	case 1: {
		// P1.0 low: code below $8000 comes from the RAM and data reads and writes go to the flash, so the
		// firmware can write the flash from a program in RAM (docs/firmware.md, "Store to flash")
		m_p1Latch = d;
		if (const bool swapped = !(d & 1); swapped != m_swapped) {
			m_swapped = swapped;
			m_mcu.setCode(swapped ? m_ram.data() : m_rom.data(), m_rom.data() + m_romBank * 0x8000);
		}
		const bool e = (d & 0x20) != 0, rw = (d & 0x40) != 0, rs = (d & 0x80) != 0;
		m_lcdLatch = uint8_t(((d << 3) & 0xf0) | 0x08);
		if (e && !m_lcdE && rw)
			m_lcdRead = m_lcd.read(rs ? 1 : 0);
		else if (!e && m_lcdE && !rw)
			m_lcd.write(rs ? 1 : 0, m_lcdLatch);
		if (!rw)
			m_lcdRead = 0xff;
		m_lcdE = e;
		break;
	}
	case 3:
		m_p3Latch = d;
		m_anSelect = (d >> 4) & 3;
		break;
	case 4:
		ledAccount();
		m_p4Latch = d;
		if (d & 0x80) // the first LED latch is open while P4 bit 7 is high
			m_latchA = d & 0x7f;
		if (m_p5Latch & 8)
			m_latchB = d & 0x7f;
		break;
	case 5:
		selectBanks(d);
		break;
	default:
		break;
	}
}

uint8_t Machine::analogIn(int channel)
{
	return uint8_t(m_knobs[size_t(4 * (channel & 7) + m_anSelect)] * 2);
}

bool Machine::rxd()
{
	const uint64_t now = m_mcu.cycles();
	if (!m_rxBusy) {
		if (m_midiIn.empty())
			return true;
		m_rxByte = m_midiIn.front();
		m_midiIn.pop_front();
		m_rxStart = now;
		m_rxBusy = true;
	}
	const uint64_t bit = (now - m_rxStart) / kMidiBitCycles;
	if (bit >= 10) {
		m_rxBusy = false;
		return true;
	}
	if (bit == 0)
		return false;
	if (bit == 9)
		return true;
	return ((m_rxByte >> (bit - 1)) & 1) != 0;
}

void Machine::txByte(uint8_t d) { m_midiOut.push_back(d); }

// ---------------------------------------------------------------- the host port

uint8_t Machine::hostRead(int offset)
{
	const uint8_t v = hostReadRegister(offset);
	hostPins();
	return v;
}

uint8_t Machine::hostReadRegister(int offset)
{
	++m_hostAccesses;
	auto &hdi = m_dsp->hdi();
	if (!m_rxValid && m_dsp->started && hdi.hasTX()) {
		m_rxWord = hdi.readTX();
		m_rxValid = true;
	}
	switch (offset) {
	case 0: return m_icr;
	case 1: return m_cvr;
	case 2: {
		uint8_t isr = 0;
		const bool txEmpty = !m_dsp->started || hdi.rxData().size() < 2;
		if (m_rxValid) isr |= ISR_RXDF;
		if (txEmpty) isr |= ISR_TXDE;
		if (!m_dsp->started || hdi.rxData().empty()) isr |= ISR_TRDY;
		const auto hcr = hdi.readControlRegister();
		isr = uint8_t(isr | ((hcr >> dsp56k::HDI08::HCR_HF2) & 3) << 3);
		if (((m_icr & ICR_RREQ) && m_rxValid) || ((m_icr & ICR_TREQ) && txEmpty)) isr |= ISR_HREQ;
		return isr;
	}
	case 3: return m_ivr;
	case 5: return uint8_t(m_rxWord >> ((m_icr & ICR_HLEND) ? 0 : 16));
	case 6: return uint8_t(m_rxWord >> 8);
	case 7: {
		const uint8_t r = uint8_t(m_rxWord >> ((m_icr & ICR_HLEND) ? 16 : 0));
		m_rxValid = false;
		return r;
	}
	default: return 0xff;
	}
}

void Machine::hostWrite(int offset, uint8_t d)
{
	hostWriteRegister(offset, d);
	hostPins();
}

void Machine::hostWriteRegister(int offset, uint8_t d)
{
	++m_hostAccesses;
	auto &hdi = m_dsp->hdi();
	switch (offset) {
	case 0:
		if ((d ^ m_icr) & (ICR_HF0 | ICR_HF1))
			hdi.setHostFlags((d & ICR_HF0) != 0, (d & ICR_HF1) != 0);
		m_icr = uint8_t(d & ~ICR_INIT);
		break;
	case 1:
		m_cvr = d;
		break;
	case 3:
		m_ivr = d;
		break;
	case 5: m_tx[(m_icr & ICR_HLEND) ? 2 : 0] = d; break;
	case 6: m_tx[1] = d; break;
	case 7: {
		m_tx[(m_icr & ICR_HLEND) ? 0 : 2] = d;
		const uint32_t word = (uint32_t(m_tx[0]) << 16) | (uint32_t(m_tx[1]) << 8) | m_tx[2];
		++m_hostWords;
		if (m_server)
			serverWord(word, d);
		else if (!m_dsp->started)
			bootWord(word);
		else
			hdi.writeRX(&word, 1);
		break;
	}
	default:
		break;
	}
}

// A host command is held until the DSP allows it (HCIE) and has room for it.
// As the HI08 does it: the DSP's word moves from its transmit register to the host's receive registers
// as soon as those are empty, and HREQ (to P3.2, the 80C515's external interrupt 0) is low while the
// host asked for receive requests (ICR RREQ) and a word is there, or for transmit requests (TREQ) and
// there is room. The firmware reads what the DSP sends unasked (MIDI out, status) in that interrupt.
void Machine::hostPins()
{
	if (!m_dsp->started)
		return;
	auto &hdi = m_dsp->hdi();
	if (!m_rxValid && hdi.hasTX()) {
		m_rxWord = hdi.readTX();
		m_rxValid = true;
	}
	const bool txEmpty = hdi.rxData().size() < 2;
	m_hreq = ((m_icr & ICR_RREQ) && m_rxValid) || ((m_icr & ICR_TREQ) && txEmpty);
	m_mcu.setInt0(!m_hreq);
}

void Machine::hostService()
{
	auto &hdi = m_dsp->hdi();
	if ((m_cvr & CVR_HC) && m_dsp->started && hdi.hostCommandInterruptEnabled() && !hdi.hostCommandsFull()) {
		hdi.injectHostCommand(dsp56k::TWord(m_cvr & 0x7f) * 2);
		m_cvr = uint8_t(m_cvr & ~CVR_HC);
	}
}

} // namespace virusc
