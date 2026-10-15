#include "machine.h"

#include <algorithm>
#include <cstring>

namespace tb303 {

namespace {
// Key matrix position: column (H bit, 0-3) and return bit (0-3 = PA0-3, 4-7 = PB0-3).
struct MatrixPos {
	uint8_t column, bit;
};
constexpr MatrixPos kKeyPos[] = {
	{0, 4}, // C   (pattern 1)
	{0, 2}, // C#  (DEL)
	{0, 5}, // D   (2)
	{0, 3}, // D#  (INS)
	{0, 6}, // E   (3)
	{0, 7}, // F   (4)
	{3, 4}, // F#
	{1, 4}, // G   (5)
	{3, 5}, // G#
	{1, 5}, // A   (6)
	{3, 6}, // A#
	{1, 6}, // B   (7)
	{1, 7}, // C'  (8)
	{2, 4}, // DOWN   (9, STEP, note)
	{2, 5}, // UP     (0, 3n, tie)
	{2, 6}, // ACCENT (100, section A, rest)
	{2, 7}, // SLIDE  (200, section B)
	{2, 0}, // PATTERN CLEAR / BAR RESET / D.C.
	{2, 1}, // FUNCTION (BAR)
	{2, 2}, // PITCH MODE
	{2, 3}, // TIME MODE
	{3, 7}, // BACK
	{0xff, 0}, // TAP: status port, not the matrix
};
static_assert(sizeof(kKeyPos) / sizeof(kKeyPos[0]) == size_t(Machine::Key::Count), "key table");

// MODE switch codes read on H0 A0-A1, by panel position.
constexpr uint8_t kModeCode[] = {3, 2, 0, 1};

// Where each panel LED gets its drive (found by running the firmware with tools/tb303seq):
// 0-15 = matrix bit (column*4 + G row), 100+n = PC bit n through the switch board's
// 47k/1uF hold drivers (D202, D203, D214, D219), -1 = the RUN flip-flop.
constexpr int8_t kLedSource[] = {
	0,   // C       D204 (H0 G0)
	12,  // C#      (H3 G0)
	1,   // D       (H0 G1)
	13,  // D#      (H3 G1)
	2,   // E       (H0 G2)
	3,   // F       (H0 G3)
	14,  // F#      (H3 G2)
	4,   // G       (H1 G0)
	15,  // G#      (H3 G3)
	5,   // A       (H1 G1)
	101, // A#      PC1
	6,   // B       (H1 G2)
	7,   // C'      (H1 G3)
	8,   // DOWN    (H2 G0)
	9,   // UP      (H2 G1)
	10,  // ACCENT  (H2 G2)
	11,  // SLIDE   (H2 G3)
	102, // PITCH MODE  PC2
	100, // TIME MODE   PC0
	103, // FUNCTION (NORMAL MODE)  PC3
	-1,  // RUN
};
static_assert(sizeof(kLedSource) == size_t(Machine::Led::Count), "led table");
} // namespace

Machine::Machine() : m_cpu(*this) {}

bool Machine::setRom(const uint8_t *data, size_t size)
{
	if (data == nullptr || size != 2048)
		return false;
	m_rom.assign(data, data + size);
	m_cpu.setRom(m_rom.data(), int(m_rom.size()));
	return true;
}

void Machine::powerOn()
{
	m_portG = 0;
	m_portH = 0xf;
	m_portI = 0;
	m_portE = 0;
	std::memset(m_addr, 0, sizeof(m_addr));
	m_ramWe = false;
	m_ramCe = false;
	m_cycleAcc = 0;
	m_irqAcc = 0;
	m_cpu.reset();
}

void Machine::setKey(Key k, bool down)
{
	if (k < Key::Count)
		m_keys[size_t(k)] = down;
}

uint8_t Machine::matrixColumn(int column) const
{
	uint8_t v = 0;
	for (size_t k = 0; k < size_t(Key::Count); ++k)
		if (m_keys[k] && kKeyPos[k].column == column)
			v |= uint8_t(1 << kKeyPos[k].bit);
	v |= m_raw[column & 3];
	if (column == 0)
		v |= kModeCode[size_t(m_mode)];
	else if (column == 1)
		v |= uint8_t(m_track & 7);
	return v;
}

uint8_t Machine::read(int port)
{
	switch (port) {
	case ucom43::A:
	case ucom43::B: {
		const uint8_t mux = m_portH ^ 0xf;
		if (port == ucom43::A && mux == 0) {
			// Status buffer & gate (IC7) when no column is selected.
			uint8_t s = 0;
			if (m_run)
				s |= 1;
			if (m_keys[size_t(Key::Tap)])
				s |= 2;
			if (m_clock)
				s |= 8;
			return s;
		}
		uint8_t data = 0;
		for (int c = 0; c < 4; ++c)
			if (mux & (1 << c))
				data |= matrixColumn(c);
		return port == ucom43::A ? (data & 0xf) : (data >> 4);
	}
	case ucom43::C:
		return (m_ramCe && !m_ramWe) ? m_xram[m_ramAddress] : 0;
	default:
		return 0;
	}
}

void Machine::refreshRam()
{
	// E2,E3 through a 4556 select one uPD444: 1 = IC-5, 2 = IC-3, 3 = IC-4.
	const int sel = (m_addr[2] >> 2) & 3;
	m_ramCe = sel != 0;
	if (!m_ramCe)
		return;
	// The address only moves while /WE is inactive.
	if (!m_ramWe)
		m_ramAddress = uint16_t((sel - 1) << 10 | (m_addr[2] & 3) << 8 | m_addr[1] << 4 | m_addr[0]);
	else
		m_xram[m_ramAddress] = m_ramData & 0xf;
}

void Machine::write(int port, uint8_t data)
{
	switch (port) {
	case ucom43::C:
		m_portC = data;
		m_ramData = data;
		refreshRam();
		break;
	case ucom43::D:
		m_addr[0] = data;
		refreshRam();
		break;
	case ucom43::F:
		m_addr[1] = data;
		refreshRam();
		break;
	case ucom43::E:
		m_addr[2] = data;
		m_portE = data;
		refreshRam();
		break;
	case ucom43::G:
		m_portG = data;
		break;
	case ucom43::H:
		m_portH = data;
		break;
	case ucom43::I: {
		const uint8_t old = m_portI;
		m_portI = data & 7;
		// PI0 drives /WE through Q4: high writes.
		m_ramWe = (data & 1) != 0;
		refreshRam();
		// PI1 rising edge: IC9 latches the pitch from D0-3/F0-1, IC13 latches accent from E0.
		if ((data & 2) && !(old & 2)) {
			m_dac = (m_addr[0] & 0xf) | (m_addr[1] & 3) << 4;
			// The ladder's LSB end returns to the 5.333 V rail, so it outputs (code + 1)/12 V:
			// the C key (code 23) is 2.000 V = C2, the A key 2.75 V = 110 Hz as calibrated.
			m_lines.note = m_dac + 13;
			m_lines.accent = (m_portE & 1) != 0;
			++m_strobes;
			m_linesChanged = true;
		}
		const bool slide = (data & 2) != 0, gate = (data & 4) != 0;
		if (slide != m_lines.slide || gate != m_lines.gate) {
			m_lines.slide = slide;
			m_lines.gate = gate;
			m_linesChanged = true;
		}
		break;
	}
	default:
		break;
	}
	// LED matrix: G drives rows of the column(s) H selects.
	if (port == ucom43::G || port == ucom43::H) {
		const uint8_t mux = m_portH ^ 0xf;
		uint16_t on = 0;
		for (int c = 0; c < 4; ++c)
			if (mux & (1 << c))
				on |= uint16_t((m_portG & 0xf) << (c * 4));
		m_ledNow = on;
	}
}

void Machine::updateLeds(double dt)
{
	// Each column is lit about a quarter of the time; average over ~30 ms like the eye.
	const float k = float(dt / 0.03);
	for (int i = 0; i < 16; ++i) {
		const float target = (m_ledNow >> i) & 1 ? 4.0f : 0.0f;
		m_ledOn[size_t(i)] += (target - m_ledOn[size_t(i)]) * k;
	}
	// PC0-3 also drive four LEDs through 47k into a transistor base with 1 uF to ground:
	// they show the average of what the firmware leaves on the data bus.
	const float kh = float(dt / 0.047);
	for (int b = 0; b < 4; ++b)
		m_pcHold[size_t(b)] += (float((m_portC >> b) & 1) - m_pcHold[size_t(b)]) * kh;
	for (size_t l = 0; l < size_t(Led::Count); ++l) {
		const int src = kLedSource[l];
		float v = src < 0 ? (m_run ? 1.0f : 0.0f) : src >= 100 ? m_pcHold[size_t(src - 100)] : m_ledOn[size_t(src)];
		m_ledLevel[l] = v > 1 ? 1 : v;
	}
}

bool Machine::tick(double rate)
{
	if (!romLoaded())
		return false;
	m_linesChanged = false;
	const double dt = 1.0 / rate;
	m_cycleAcc += kCycleHz * dt;
	m_irqAcc += dt;
	if (m_irqAcc >= m_irqSeconds) {
		m_irqAcc -= m_irqSeconds;
		m_cpu.setInt(true);
		m_cpu.setInt(false);
	}
	if (m_cycleAcc >= 1) {
		const int want = int(m_cycleAcc);
		m_cycleAcc -= m_cpu.run(want);
	}
	updateLeds(dt);
	return m_linesChanged;
}

void Machine::writePattern(int group, int section, int pattern, const std::vector<Step> &steps, bool triplet)
{
	const int base = patternAddress(group, section, pattern);
	const int maxSteps = triplet ? 15 : 16;
	const int n = std::min(int(steps.size()), maxSteps);
	for (int i = 0; i < 16; ++i) {
		m_xram[size_t(base + i)] = 0xf;
		m_xram[size_t(base + 0x80 + i)] = 0;
	}
	int noteStart = -1;
	for (int i = 0; i < n; ++i) {
		const Step &st = steps[size_t(i)];
		const bool tie = st.time == Step::Tie && noteStart >= 0;
		if (st.time == Step::Rest) {
			noteStart = -1;
			continue;
		}
		const Step &src = tie ? steps[size_t(noteStart)] : st;
		if (!tie)
			noteStart = i;
		uint8_t flags = uint8_t((src.down ? 1 : 0) | (src.up ? 2 : 0) | (src.accent ? 4 : 0));
		// A note's own SLIDE applies after its last tied step; earlier steps slide into the tie.
		const bool nextTies = i + 1 < n && steps[size_t(i + 1)].time == Step::Tie;
		if (nextTies || src.slide)
			flags |= 8;
		m_xram[size_t(base + i)] = uint8_t(std::min<int>(src.key, 12));
		m_xram[size_t(base + 0x80 + i)] = flags;
		if (tie) {
			// the step before carries the slide into this one
			m_xram[size_t(base + 0x80 + i - 1)] |= 8;
		}
	}
	if (n < maxSteps)
		m_xram[size_t(base + n)] = 0xe;
	if (triplet)
		m_xram[size_t(base + 15)] = 0xd;
}

void Machine::clearPatterns()
{
	for (int g = 0; g < 4; ++g)
		for (int s = 0; s < 2; ++s)
			for (int p = 0; p < 8; ++p)
				for (int i = 0; i < 16; ++i)
					m_xram[size_t(patternAddress(g, s, p) + i)] = 0xf;
}

void Machine::setPatternRam(const uint8_t *data, size_t n)
{
	for (size_t i = 0; i < m_xram.size(); ++i)
		m_xram[i] = i < n ? data[i] & 0xf : 0;
}

namespace {
constexpr uint32_t kStateMagic = 0x33303301; // "303" v1
template <typename T> void put(std::vector<uint8_t> &v, const T &x)
{
	const auto *p = reinterpret_cast<const uint8_t *>(&x);
	v.insert(v.end(), p, p + sizeof(T));
}
template <typename T> bool get(const std::vector<uint8_t> &v, size_t &pos, T &x)
{
	if (pos + sizeof(T) > v.size())
		return false;
	std::memcpy(&x, v.data() + pos, sizeof(T));
	pos += sizeof(T);
	return true;
}
} // namespace

std::vector<uint8_t> Machine::saveState() const
{
	std::vector<uint8_t> v;
	put(v, kStateMagic);
	v.insert(v.end(), m_xram.begin(), m_xram.end());
	ucom43::Cpu::State s{};
	m_cpu.save(s);
	put(v, s);
	put(v, m_addr);
	put(v, m_ramAddress);
	put(v, m_ramData);
	put(v, m_portG);
	put(v, m_portH);
	put(v, m_portI);
	put(v, m_portE);
	put(v, m_dac);
	put(v, m_lines);
	return v;
}

bool Machine::loadState(const std::vector<uint8_t> &v)
{
	size_t pos = 0;
	uint32_t magic = 0;
	if (!get(v, pos, magic) || magic != kStateMagic || pos + m_xram.size() > v.size())
		return false;
	std::memcpy(m_xram.data(), v.data() + pos, m_xram.size());
	pos += m_xram.size();
	ucom43::Cpu::State s{};
	if (!get(v, pos, s))
		return true; // RAM only: cold boot
	m_cpu.load(s);
	get(v, pos, m_addr);
	get(v, pos, m_ramAddress);
	get(v, pos, m_ramData);
	get(v, pos, m_portG);
	get(v, pos, m_portH);
	get(v, pos, m_portI);
	get(v, pos, m_portE);
	get(v, pos, m_dac);
	get(v, pos, m_lines);
	m_ramWe = (m_portI & 1) != 0;
	const int sel = (m_addr[2] >> 2) & 3;
	m_ramCe = sel != 0;
	return true;
}

} // namespace tb303
