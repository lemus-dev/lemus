#include "board.h"

#include <algorithm>

namespace bsrack {

const char *const kMuxPotNames[17] = {
	"osc LFO depth",     // 0: C5/D7 (Osc 1/2 select), 6C64
	"mix",               // 1: C3, 6B96: the osc levels (x'34')/(x'32')
	"semitone",          // 2: C2, 6BF6: 13 steps
	"LFO speed",         // 3: C7, 706E: off at 3 and below (655A)
	"osc env depth",     // 4: C4/D6, 6CE1
	"LFO delay",         // 5: C8, 7078
	"detune",            // 6: C1, 6C15: table 0800, dead band 77-89
	"portamento",        // 7: C0, 6A87: off at 3 and below
	"env release",       // 8: CF/D4 (Env 1/2 select), 700A, controller 111/117
	"filter mod depth",  // 9: CB (LFO) / D9 (Env 2), 6DE9, controller 107
	"env decay",         // 10: CD/D2, 6FA7, controller 109/115
	"filter frequency",  // 11: C9, 6D4E, controller 105
	"env sustain",       // 12: CE/D3, 6F4B, controller 110/116
	"resonance",         // 13: CA, 6D86, controller 106
	"env attack",        // 14: CC/D1, 6ED8, controller 108/114
	"pulse width",       // 15: C6/D8 (Osc 1/2 select), 6AA4
	"env velocity",      // 16 (ADC channel 3): D0/D5, 6E96, controller 112/118
};

const std::array<int, kPots> kPotMux = {
	-1,  // kVolume: not read
	7,   // kPortamento
	3,   // kLfoSpeed
	5,   // kLfoDelay
	1,   // kMix
	6,   // kDetune
	2,   // kSemitone
	4,   // kModEnvelope
	0,   // kModLfo
	15,  // kPulseWidth
	11,  // kFrequency
	13,  // kResonance
	9,   // kModDepth
	14,  // kAttack
	10,  // kDecay
	12,  // kSustain
	8,   // kRelease
	16,  // kVelocity
};

// From the switch decoder (65A1-6797): the contacts per row, which switch each decodes into, and
// v1.5's validity lists (a row is ignored unless each multi-way switch has one contact closed).
// The positions' meanings: measured for range, sync, PWM source, waveforms, slope, select,
// envelope select, mod source and write; [inferred] from the panel order for the LFO shape
// (codes 0/1/2 in E0 bits 0-1) and triggering (codes 20/10/00 in E1 bits 4-5; 20 doesn't
// retrigger, as the factory "AUTOGLIDE" program has it).
const std::array<SwitchContacts, kSwitches> kSwitchContacts = {{
	{1, 0x01, {0x01, 0x00, 0, 0}},        // kWrite: PROTECT closes row 1 bit 0 ((x'B6') bit 6 = 0)
	{3, 0x38, {0x08, 0x10, 0x20, 0}},     // kLfoShape: E0 bits 0-1 = 0, 1, 2
	{1, 0x02, {0x02, 0x00, 0, 0}},        // kOsc1Wave: saw closes (E0 bit 7 = 1, port 1 bit 7 = 0)
	{0, 0x0f, {0x01, 0x02, 0x04, 0x08}},  // kOsc2Range: E0 bits 5-6 = 00, 20, 40, 60
	{1, 0x04, {0x04, 0x00, 0, 0}},        // kOsc2Wave: saw closes (E0 bit 4 = 1, port 1 bit 6 = 0)
	{3, 0x01, {0x01, 0x00, 0, 0}},        // kSync: OFF closes (E1 bit 6 = 0)
	{1, 0x08, {0x08, 0x00, 0, 0}},        // kOscillator: 1 closes ((x'FC') bit 6 = 0)
	{2, 0x07, {0x01, 0x02, 0x04, 0}},     // kPwmSource: ENV 2, MAN., LFO (E1 osc 1 bits 2-3 = 2, 1, 0)
	{1, 0x20, {0x20, 0x00, 0, 0}},        // kCutOff: 12dB closes (E0 bit 2 = 1, port 3 bit 1 = 1)
	{1, 0x10, {0x10, 0x00, 0, 0}},        // kModSource: LFO closes (E0 bit 3 = 0)
	{0, 0x30, {0x10, 0x00, 0x20, 0}},     // kEnvelope: 1, 2 (no contact), 1&2 ((x'FC') bits 2/1)
	{2, 0x38, {0x08, 0x10, 0x20, 0}},     // kTriggering: E1 bits 4-5 = 20, 10, 00
}};

const std::array<int, kButtons> kKeyCode = {
	0,   // kKey1
	4,   // kKey2
	8,   // kKey3
	12,  // kKey4
	1,   // kKey5
	5,   // kKey6
	9,   // kKey7
	13,  // kKey8
	2,   // kKey9
	6,   // kKeyMinus
	3,   // kKey0 (codes 7, 10 and 11 also read as 0: no keys there)
	14,  // kKeyPlus
	-1,  // kAudition: port 2 bit 4
	15,  // kMode
};

Board::Board()
{
	eeprom = defaultEeprom();
	pots.fill(0x80);
	switchRows.fill(0x3f);
	keyRows.fill(0x3f);
	const std::array<int, kSwitches> positions{0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 2};
	for (int s = 0; s < kSwitches; ++s)
		setSwitch(s, positions[size_t(s)]);

	cpu.set_bus(this);
	cpu.set_ram_end(0x03cf);              // MAME's basssta.cpp: RAM 0060-03CF; v1.5 clears it all (35C1)
	cpu.set_adc_channel_register(0x1e);   // [inferred] 34AD-34EC
	cpu.set_port_input_mask(2, 0x50);
	cpu.set_port_input_mask(9, 0x3c);
	cpu.port_in_cb<2>() = [this]() -> u8 { return u8(0xff & ~(gateIn ? 0 : 0x40) & ~(audition ? 0x10 : 0)); };
	cpu.port_in_cb<9>() = [this]() -> u8 {
		u8 v = 0xff;
		for (int r = 0; r < 4; r++)
			if (!BIT(a800, 4 + r))
				v &= u8(keyRows[size_t(r)] | 0xc3);
		return v;
	};
	cpu.ad_in_cb<0>() = [this]() -> u8 { adc(0); return pots[a000 & 0x0f]; };
	cpu.ad_in_cb<1>() = [this]() -> u8 { adc(1); return cvIn; };
	cpu.ad_in_cb<2>() = [this]() -> u8 { adc(2); return 0; };
	cpu.ad_in_cb<3>() = [this]() -> u8 { adc(3); return pots[16]; };
	cpu.ad_in_cb<4>() = [this]() -> u8 { adc(4); return 0; };
	cpu.ad_in_cb<5>() = [this]() -> u8 { adc(5); return 0; };
	cpu.ad_in_cb<6>() = [this]() -> u8 { adc(6); return 0; };
	cpu.ad_in_cb<7>() = [this]() -> u8 { adc(7); return 0; };
	cpu.port_out_cb<1>() = [this](u8 d) { port1Write(d); };
	cpu.port_out_cb<3>() = [this](u8 d) { port3Write(d); };
	cpu.timer1_out_cb() = [this](int) {
		if (onSync)
			onSync(1, cpu.time_clocks());
	};
	cpu.uart_tx_byte_cb() = [this](u8 d) {
		if (onMidiOut)
			onMidiOut(d);
	};
}

// LEMUS: a patch for a lock-up in v1.5's pot scan (docs/firmware.md, "A lock-up in v1.5").
// The main loop reads two pots per 4 ms tick: the next "active" one (a pot that moved: its flag at
// 0280 + n is set to FF when it moves, 699C, and counts down by one each time it is scanned, 697A)
// and the next idle one, which sub_6A10 finds by looking for a flag of zero, called every tick
// from 5475. 6A10's loop (6A16-6A26) has no exit: with all 17 flags non-zero at once it spins
// forever, the main loop stops, and the unit no longer plays notes, the AUDITION key or anything
// else until it is switched off (the interrupts still run). One pot's flag lasts about 1 s, 17
// pots' about 17 s, so turning all 17 knobs about 0.3 s apart reaches it, and so does a host
// moving all the pots at once. The patch calls a check in unused ROM (356B-3578, all FF in v1.5)
// in place of 6A10 at 5475: it calls 6A10 only if some flag is zero, and returns otherwise (the idle
// pointer then waits a tick). Outside that state the firmware runs as it is.
bool Board::setRom(const std::vector<uint8_t> &data, bool fixPotScanLock)
{
	if (data.size() != kRomSize)
		return false;
	std::copy(data.begin(), data.end(), rom.begin());
	m_potScanFix = false;
	if (!fixPotScanLock)
		return true;
	static const uint8_t call_at_5475[3] = {0xf7, 0x6a, 0x10};  // call /x'6A10'
	static const uint8_t check[] = {
		0x31, 0x80, 0x02,  // 356B  movl    xp, x'0280'        the first pot's flag
		0xf8, 0x00, 0x06,  // 356E  cmpbe   (xp), x'00', 3577  an idle pot: go on to 6A10
		0xe5,              // 3571  inc     xp
		0xd1, 0x91,        // 3572  cmp     xpl, x'91'         past the 17th
		0x8a, 0xf8,        // 3574  bnz     356E
		0xf4,              // 3576  ret                         none idle: skip 6A10 this tick
		0xf6, 0x6a, 0x10,  // 3577  br      /x'6A10'
	};
	if (!std::equal(call_at_5475, call_at_5475 + 3, rom.begin() + 0x5475))
		return true;
	for (size_t i = 0; i < sizeof(check); i++)
		if (rom[0x356b + i] != 0xff)
			return true;
	std::copy(check, check + sizeof(check), rom.begin() + 0x356b);
	rom[0x5476] = 0x35;  // call /x'356B'
	rom[0x5477] = 0x6b;
	m_potScanFix = true;
	return true;
}

void Board::setSwitch(int sw, int pos)
{
	if (sw < 0 || sw >= kSwitches)
		return;
	const SwitchContacts &c = kSwitchContacts[size_t(sw)];
	pos = std::clamp(pos, 0, kSwitchPositions[size_t(sw)] - 1);
	uint8_t &row = switchRows[size_t(c.row)];
	row = uint8_t((row | c.mask) & ~c.closed[size_t(pos)]);
}

void Board::setKey(int code, bool down)
{
	if (code < 0 || code > 15)
		return;
	static const int column_bit[4] = {4, 5, 3, 2};
	const uint8_t bit = uint8_t(1 << column_bit[code & 3]);
	uint8_t &row = keyRows[size_t(code >> 2)];
	row = down ? uint8_t(row & ~bit) : uint8_t(row | bit);
}

std::array<uint8_t, 34> Board::defaultProgram()
{
	// Pot bytes run 03-FC (the factory programs' range); the bipolar ones are centred at 80. Where
	// "basic square wave" doesn't decide a byte, it is what most of the 40 factory programs have.
	return {
		0x03,  //  0 C0 portamento: off
		0x80,  //  1 C1 detune: centre
		0x03,  //  2 C2 semitone: 0
		0x50,  //  3 C3 mix: osc 1 only (level tables 0500/0600 give FE and 00)
		0x80,  //  4 C4 osc 1 envelope depth: centre
		0x80,  //  5 C5 osc 1 LFO depth: centre
		0x03,  //  6 C6 osc 1 pulse width: square
		0x9c,  //  7 C7 LFO speed: 9C, as 19 of the 40
		0x03,  //  8 C8 LFO delay: none
		0xfc,  //  9 C9 filter frequency: open
		0x03,  // 10 CA resonance: none
		0x80,  // 11 CB filter mod depth (LFO): centre
		0x03,  // 12 CC env 1 attack
		0x03,  // 13 CD env 1 decay
		0xfc,  // 14 CE env 1 sustain: full
		0x03,  // 15 CF env 1 release
		0x03,  // 16 D0 env 1 velocity: none
		0x03,  // 17 D1 env 2 attack
		0x03,  // 18 D2 env 2 decay
		0xfc,  // 19 D3 env 2 sustain
		0x03,  // 20 D4 env 2 release
		0x03,  // 21 D5 env 2 velocity
		0x80,  // 22 D6 osc 2 envelope depth: centre
		0x80,  // 23 D7 osc 2 LFO depth: centre
		0x03,  // 24 D8 osc 2 pulse width: square
		0x80,  // 25 D9 filter mod depth (Env 2): centre
		0x05,  // 26 DA utility nibbles: 05, as 36 of the 40
		0x00,  // 27 DB utility nibbles: 00, as all 40
		0x00,  // 28 DC utility nibbles: 00, as all 40
		0x00,  // 29 DD utility nibbles: 00, as all 40
		0x02,  // 30 DE pitch bend range: 2 semitones, as 36 of the 40
		0x00,  // 31 DF MIDI-to-LFO clock rate: off, as all 40
		0x29,  // 32 E0: both oscillators pulse, osc 2 8', mod source Env 2 (as 36 of 40), 24 dB,
		       //        LFO shape code 1 (as 39 of 40)
		0x05,  // 33 E1: sync off, triggering code 00 (as 34 of 40), PWM source MAN. for both
	};
}

std::array<uint8_t, Board::kEepromSize> Board::defaultEeprom()
{
	std::array<uint8_t, kEepromSize> e;
	e.fill(0xff);
	const std::array<uint8_t, 34> p = defaultProgram();
	for (size_t i = 0; i < 60; i++)
		std::copy(p.begin(), p.end(), e.begin() + std::ptrdiff_t(i * p.size()));
	const uint8_t globals[8] = {
		0x00,  // 07F8 bit 0: poly mode off ((x'F8') bit 6, 72C9)
		0x00,  // 07F9: CV/Gate type 00 (0x18B, 72B6)
		0x01,  // 07FA: CV/Gate MIDI channel 1 (0x18A, 72A9)
		0x01,  // 07FB: MIDI TX channel 1 (0x189, 729C)
		0x01,  // 07FC: MIDI RX channel 1 (0x188, 728F)
		0x00,  // 07FD: program 00 (0x187, 7282)
		0x00,  // 07FE: CV IN scale trim (signed, (x'8C'), 78F9): none
		0x00,  // 07FF: CV IN offset trim (signed, (x'9D'), 7940): none
	};
	std::copy(globals, globals + 8, e.begin() + 0x7f8);
	return e;
}

uint8_t Board::segments(uint8_t v)
{
	return uint8_t(BIT(v, 2) | BIT(v, 3) << 1 | BIT(v, 4) << 2 | BIT(v, 7) << 3 | BIT(v, 5) << 4 | BIT(v, 1) << 5 |
	               BIT(v, 0) << 6 | BIT(v, 6) << 7);
}

uint8_t Board::read_program(uint16_t addr)
{
	if (addr < kRomSize)
		return rom[addr];
	if (onUnmapped)
		onUnmapped(uint16_t(addr), -2);
	return 0;
}

uint8_t Board::read_data(uint16_t addr)
{
	if (addr >= 0x8000 && addr < 0x8800)
		return eeprom[addr - 0x8000];
	if (addr == 0x8800)
	{
		// rows: A800 bit 3 - row (33BF)
		uint8_t v = 0xff;
		for (int r = 0; r < 4; r++)
			if (!BIT(a800, 3 - r))
				v &= uint8_t(switchRows[size_t(r)] | 0xc0);
		return v;
	}
	if (onUnmapped)
		onUnmapped(addr, -1);
	return 0;
}

void Board::write_data(uint16_t addr, uint8_t data)
{
	if (addr >= 0x8000 && addr < 0x8800)
	{
		// v1.5 polls until a read gives the byte back (3551); the write is immediate here
		eeprom[addr - 0x8000] = data;
		return;
	}
	switch (addr)
	{
	case 0xa000:
		a000 = data;
		if (BIT(a000, 7))
			sample();
		break;
	case 0xa800:
		a800 = data;
		break;
	case 0xb000:
		b000 = data;
		break;
	case 0xb800:
		b800 = data;
		break;
	default:
		if (onUnmapped)
			onUnmapped(addr, data);
		return;
	}
	if (onLatch)
		onLatch(addr, data);
}

// The CV DAC: 12 bits shifted in MSB first on port 1 bit 0, clocked by port 1 bit 2 rising; the pulse
// on port 1 bit 1 loads them into the output. v1.5 pulses it before it shifts the next word, while
// the S&H is off, so a channel's S&H takes the word shifted in the slot before: the slot for
// channel c shifts word 0220 + 2(c + 1), and channel c holds 0220 + 2c [inferred].
void Board::port1Write(uint8_t d)
{
	const uint8_t rise = uint8_t(~m_port1 & d);
	if (BIT(rise, 2))
		m_dacShift = uint16_t((m_dacShift << 1 | BIT(d, 0)) & 0x0fff);
	if (BIT(rise, 1))
		dacOut = m_dacShift;
	m_port1 = d;
}

void Board::port3Write(uint8_t d)
{
	const uint8_t changed = uint8_t(d ^ m_port3);
	if (BIT(changed, 3) && onSync)
		onSync(0, cpu.time_clocks());
	// the digit select flips after the latch is written (3498, 34A4)
	if (BIT(changed, 5))
		digits[BIT(d, 5)] = b800;
	m_port3 = d;
}

void Board::sample()
{
	const int ch = (a000 >> 4) & 7;
	if (cv[size_t(ch)] != dacOut || !m_sampled[size_t(ch)])
	{
		cv[size_t(ch)] = dacOut;
		m_sampled[size_t(ch)] = true;
		if (onCv)
			onCv(ch, dacOut);
	}
}

void Board::adc(int channel)
{
	if (onAdc)
		onAdc(channel, a000 & 0x0f);
}

void Board::saveState(std::vector<uint8_t> &v) const
{
	using lemus_state::put;
	cpu.save_state(v);
	put(v, eeprom);
	put(v, pots);
	put(v, cvIn);
	put(v, switchRows);
	put(v, keyRows);
	put(v, gateIn);
	put(v, audition);
	put(v, a000);
	put(v, a800);
	put(v, b000);
	put(v, b800);
	put(v, digits);
	put(v, cv);
	put(v, dacOut);
	put(v, m_port1);
	put(v, m_port3);
	put(v, m_dacShift);
	put(v, m_sampled);
}

bool Board::loadState(const uint8_t *&p, const uint8_t *end)
{
	using lemus_state::get;
	return cpu.load_state(p, end) && get(p, end, eeprom) && get(p, end, pots) && get(p, end, cvIn) && get(p, end, switchRows) &&
	       get(p, end, keyRows) && get(p, end, gateIn) && get(p, end, audition) && get(p, end, a000) && get(p, end, a800) &&
	       get(p, end, b000) && get(p, end, b800) && get(p, end, digits) && get(p, end, cv) && get(p, end, dacOut) &&
	       get(p, end, m_port1) && get(p, end, m_port3) && get(p, end, m_dacShift) && get(p, end, m_sampled);
}

} // namespace bsrack
