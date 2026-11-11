#include "machine.h"

#include "voiceboard.h"

#include <cstring>
#include <string>

// The TR-808's factory rhythms, from the rhythm charts printed with it (the "Rhythm Chart" pages
// RC p.2-7; docs/sequencer.md). The memory list on RC p.1 says what each key held:
//
//   BASIC RHYTHM 1, 2, I/F 4   "Practice Program": no chart, left empty here
//   BASIC RHYTHM 3-6           ROCK, BOSSA NOVA, MAMBO, SAMBA
//   BASIC RHYTHM 7-11          "The Rhythm For Track Memory" (what the factory tracks 1 and 2 play)
//   BASIC RHYTHM 12            "The Rhythm For Checking Sounds": every voice on every step
//   INTRO/FILL IN 1-3          "Fill in 1-3"; only 1 has a B variation charted
//
// The factory rhythm tracks (1 = rock, 2 = latin) aren't in any document we have, so the tracks
// are left empty. A row names the RAM row: LT plays LC, MT MC, HT HC, RS CL and CP MA when the
// voice switches say so. `x` is a hit, `a` an accent, `|` the end of the 1st part.

namespace tr808 {
namespace {

struct Chart {
	int key;      // 0-11 BASIC RHYTHM 1-12, 12-15 INTRO/FILL IN 1-4
	int prescale; // 0-3 = PRE-SCALE 1-4
	int variation;
	const char *rows;
};

const Chart kCharts[] = {
	// BR-3 ROCK (4/4 = 16 steps, pre-scale 3) [RC p.2]
	{2, 2, 0, "BD x......xx....x.. SD ....x.xx.x..x.x. CH x.xxx.xxx.xxx.xx AC a...........a.a."},
	{2, 2, 1, "BD x.x..x.xx.x..x.. SD ....xx.x.x..xx.x OH ..x...x...x...x. CH xx..xx..xx..xx.. AC a........a..a..."},
	// BR-4 BOSSA NOVA (4/4 = 16 steps, pre-scale 3) [RC p.2]
	{3, 2, 0, "BD x..xx..xx..xx..x RS x..x..x...x..x.. CP xxxxxxxxxxxxxxxx CH x.x.x.x.x.x.x.x. AC .......a.......a"},
	{3, 2, 1, "BD x..xx..xx..xx..x RS x.x..x..x..x..x. CP xxxxxxxxxxxxxxxx CH x.x.x.x.x.x.x.x. AC .......a.......a"},
	// BR-5 MAMBO (4/4 = 16 steps, pre-scale 3) [RC p.3]
	{4, 2, 0, "BD x.......x.....x. LT ............x.x. MT ......x.x....... CP x.x.x.x.x.x.x.x. CB x...x...x...x... CH x.x.x.x.x.x.x.x. AC ......a.a.....a."},
	{4, 2, 1, "BD x.....x.x.....x. LT ............x.x. MT ......x...x..... CP x.x.x.x.x.x.x.x. CB x...x...x...x... CH x.x.x.x.x.x.x.x. AC ......a...a...a."},
	// BR-6 SAMBA (4/4 = 16 steps, pre-scale 3) [RC p.3]
	{5, 2, 0, "BD x.......x..x...x MT ....xx......xxx. HT .......x.x.x.... CP xxxxxxxxxxxxxxxx CB x.x....x.x.x.... CH x.x.x.x.x.x.x.x. AC ..a...a...a...a."},
	{5, 2, 1, "BD x...x...x..xx..x SD ...xxxx..xxxxx.x MT ......xx......xx HT ...xx......xx... CP x.xxx.xxx.xxx.xx CB x.x.xx.x.xx.x.x. CH x.x.x.x.x.x.x.x. AC ..a...a...a...a."},
	// BR-7 (track rhythm) (4/4 = 16 steps, pre-scale 3) [RC p.4]
	{6, 2, 0, "BD x...x...x...x... SD ....x.......x.xx OH ..x...x...x...x. AC a.a.a.a.a...a.a."},
	{6, 2, 1, "CP x..x..x...x.x... AC a..............."},
	// BR-8 (track rhythm) (4/4 = 16 steps, pre-scale 3) [RC p.4]
	{7, 2, 0, "BD x...x...x...x... SD ....x.......x... OH ..x...x.......x. CH ........xxxx.... AC a.a.a.a.a.a.a.a."},
	{7, 2, 1, "BD x...x...x...x... CP x..x..x...x.x... OH ..........x..... CH xxxxxxxxxx.xxxxx"},
	// BR-9 (track rhythm) (4/4 = 16 steps, pre-scale 3) [RC p.5]
	{8, 2, 0, "BD x..x.....x.x..x. SD ....x.......x... OH .........x.x.... CH xxxxxxxxx.x.xxxx AC a.aaa.a..a.aa.a."},
	{8, 2, 1, "BD x...x...x...x... LT ......xx......xx MT ..x.......x..... CP x..x..x...x.x... OH ..........x..... CH xxxxxxxxxx.xxxxx AC a.........a....."},
	// BR-10 (track rhythm) (4/4 = 16 steps, pre-scale 3) [RC p.5]
	{9, 2, 0, "BD x..x....x.x..x.. SD ....x.......x... CY x.x.x.x.x.x.x.x. CH .x.x.x.x.x.x.x.x AC a.a.a.a...a.a.a."},
	{9, 2, 1, "BD x...x...x...x... LT ......xx......xx MT ..x.......x..... RS x..x..x...x.x... CP x..x..x...x.x... CB x.x.xx.xx.xx.x.x OH ..........x..... CH xxxxxxxxxx.xxxxx"},
	// BR-11 (track rhythm) (4/4 = 16 steps, pre-scale 3) [RC p.6]
	{10, 2, 0, "BD x............... CY x............... AC a..............."},
	{10, 2, 1, "BD x...x...x...x... MT x.x.xx.xx....... RS x.x.xx.xx....... CP x.x.xx.xx....... CB x.x.xx.xx....... OH ............x... CH xxxxxxxxx....... AC ........a...a..."},
	// BR-12 "The Rhythm For Checking Sounds" [RC p.6]: notation only, every voice on all 16
	// sixteenths, B a repeat of A. Whether it accents can't be read from it; it doesn't here.
	{11, 2, 0, "BD xxxxxxxxxxxxxxxx SD xxxxxxxxxxxxxxxx LT xxxxxxxxxxxxxxxx MT xxxxxxxxxxxxxxxx HT xxxxxxxxxxxxxxxx RS xxxxxxxxxxxxxxxx CP xxxxxxxxxxxxxxxx CB xxxxxxxxxxxxxxxx CY xxxxxxxxxxxxxxxx OH xxxxxxxxxxxxxxxx CH xxxxxxxxxxxxxxxx"},
	{11, 2, 1, "BD xxxxxxxxxxxxxxxx SD xxxxxxxxxxxxxxxx LT xxxxxxxxxxxxxxxx MT xxxxxxxxxxxxxxxx HT xxxxxxxxxxxxxxxx RS xxxxxxxxxxxxxxxx CP xxxxxxxxxxxxxxxx CB xxxxxxxxxxxxxxxx CY xxxxxxxxxxxxxxxx OH xxxxxxxxxxxxxxxx CH xxxxxxxxxxxxxxxx"},
	// I/F-1 "Fill in 1" (4/4 = 16 steps, pre-scale 3) [RC p.7]
	{12, 2, 0, "BD .........x.x.x.x SD xx......x....... LT ......xx........ MT ....xx.......... HT ..xx............ OH .........x.x.x.. AC a.a.a.a.aa.a.a.."},
	{12, 2, 1, "BD x...x...x...x... LT x...xx......xx.x MT ...x..xx..xxxx.x HT ..x.....xx...... CP ............xx.x CB ............xx.x OH ..........x..... CH xxxxxxxxxx.xxxxx AC ..a............."},
	// I/F-2 "Fill in 2" (4/4 = 16 steps, pre-scale 3, A only charted) [RC p.7]
	{13, 2, 0, "BD .x.x.x.x.x.x.x.x SD x............... OH .x.x.x.x.x.x.x.x AC a..............."},
	// I/F-3 "Fill in 3" (4/4 = 24 steps, 1st 12 | 2nd 12, pre-scale 2, A only charted) [RC p.7]
	{14, 1, 0, "SD xxxxxx......|............ LT ............|......xxxxxx MT ............|xxxxxx...... HT ......xxxxxx|............ AC a.....a.....|a.....a....."},
};

uint16_t rowBit(const std::string &name)
{
	static const struct { const char *name; uint16_t bit; } bits[] = {
		{"BD", kBitBD}, {"SD", kBitSD}, {"LT", kBitLT}, {"MT", kBitMT}, {"HT", kBitHT}, {"RS", kBitRS},
		{"CP", kBitCP}, {"CB", kBitCB}, {"CY", kBitCY}, {"OH", kBitOH}, {"CH", kBitCH}, {"AC", kBitAC},
	};
	for (const auto &b : bits)
		if (name == b.name)
			return b.bit;
	return 0;
}

} // namespace

void Machine::writeFactoryRhythms()
{
	m_xram.fill(0);
	// Every key as PATTERN CLEAR leaves it (16 steps, no 2nd part) at PRE-SCALE 3, then the charts.
	for (int key = 0; key < 16; ++key)
		for (int v = 0; v < 2; ++v)
			writePattern(key, v, std::vector<uint16_t>(16, 0), {}, 2);
	for (const auto &c : kCharts) {
		std::vector<uint16_t> parts[2];
		const char *p = c.rows;
		while (*p != 0) {
			while (*p == ' ')
				++p;
			const char *name = p;
			while (*p != ' ' && *p != 0)
				++p;
			const uint16_t bit = rowBit(std::string(name, size_t(p - name)));
			while (*p == ' ')
				++p;
			int part = 0;
			size_t step = 0;
			for (; *p != ' ' && *p != 0; ++p) {
				if (*p == '|') {
					part = 1;
					step = 0;
					continue;
				}
				auto &steps = parts[part];
				if (steps.size() <= step)
					steps.resize(step + 1, 0);
				if (*p != '.')
					steps[step] |= bit;
				++step;
			}
		}
		writePattern(c.key, c.variation, parts[0], parts[1], c.prescale);
	}
	// I/F 2 and 3, charted with A only, keep an empty B, of A's lengths and pre-scale (the two share
	// them).
}

} // namespace tr808
