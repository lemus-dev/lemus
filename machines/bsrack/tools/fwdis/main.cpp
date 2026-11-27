// bsrackdis: a recursive-descent listing of the Bass Station Rack firmware, with MAME's MN1880
// disassembler (core/mn1880/mn1880d.cpp). Code is followed from the vectors (0000-003F, big-endian
// words) and from every branch and call target; what is never reached is listed as data. A REP n
// prefix gives the next instruction n more operand sets, each listed (and followed) on its own line.
// Indirect jumps (br xp, call xp) and rdtbl tables can't be followed; give more entry points with
// -e ADDR, or -t ADDR:N for a table of N big-endian words (v1.5: -t 352F:17, the pot handlers).
//
// Build: the bsrackdis target (machine.cmake), no JUCE.
// Run: bsrackdis <v1.5.bin> [-e ADDR]... [-t ADDR:N]... > listing.txt
#include "bsrack/core/mn1880/mn1880d.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<u8> g_rom;

struct Insn
{
	int length = 0;      // bytes of this line
	std::string text;
	std::vector<u16> targets;
	bool call = false;   // targets are subroutines
	bool ends = false;   // no fall-through
};

// Disassembles at pc; with `opcode` >= 0, pc is the start of a REP's extra operand set and the
// opcode byte is taken as sitting just before it.
Insn decode(u16 pc, int opcode = -1)
{
	mn1880_disassembler dis;
	util::disasm_interface &d = dis;
	const u16 at = opcode >= 0 ? u16(pc - 1) : pc;
	util::disasm_interface::data_buffer buf([at, opcode] (offs_t a) -> u8 {
		if (opcode >= 0 && a == at)
			return u8(opcode);
		return a < g_rom.size() ? g_rom[a] : 0xff;
	});
	std::ostringstream s;
	const offs_t r = d.disassemble(s, at, buf, buf);
	Insn insn;
	insn.length = int(r & util::disasm_interface::LENGTHMASK) - (opcode >= 0 ? 1 : 0);
	insn.text = s.str();
	const u8 op = opcode >= 0 ? u8(opcode) : g_rom[pc];

	// targets: $x'....' relative, /x'....' absolute, and the 4K-page br/call operand
	for (size_t p = 0; (p = insn.text.find("x'", p)) != std::string::npos; p += 2)
	{
		const bool rel = p > 0 && insn.text[p - 1] == '$';
		const bool abs = p > 0 && insn.text[p - 1] == '/';
		const bool page = (op >= 0x90 && op <= 0xaf);
		if (!rel && !abs && !page)
			continue;
		if (insn.text.size() < p + 7 || insn.text[p + 6] != '\'')
			continue;
		insn.targets.push_back(u16(std::strtol(insn.text.substr(p + 2, 4).c_str(), nullptr, 16)));
	}
	insn.call = (op >= 0x90 && op <= 0x9f) || op == 0xf7 || op == 0xfd;
	insn.ends = op == 0x88 || (op >= 0xa0 && op <= 0xaf) || op == 0xf4 || op == 0xf5 || op == 0xf6 || op == 0xfc;
	if (insn.length < 1)
		insn.length = 1;
	return insn;
}

} // namespace

int main(int argc, char **argv)
{
	const char *path = nullptr;
	std::deque<u16> work;
	std::map<u16, std::string> labels;
	std::vector<std::pair<u16, int>> tables;
	for (int i = 1; i < argc; i++)
	{
		if (!std::strcmp(argv[i], "-e") && i + 1 < argc)
		{
			const u16 a = u16(std::strtol(argv[++i], nullptr, 16));
			work.push_back(a);
			labels[a] = "entry";
		}
		else if (!std::strcmp(argv[i], "-t") && i + 1 < argc)
		{
			char *colon = nullptr;
			const u16 a = u16(std::strtol(argv[++i], &colon, 16));
			tables.push_back({a, colon && *colon == ':' ? std::atoi(colon + 1) : 1});
		}
		else
			path = argv[i];
	}
	if (!path)
	{
		std::fprintf(stderr, "usage: bsrackdis <rom> [-e ADDR]... [-t ADDR:N]...\n");
		return 2;
	}
	FILE *f = std::fopen(path, "rb");
	if (!f)
	{
		std::perror(path);
		return 1;
	}
	g_rom.assign(0x8000, 0xff);
	const size_t got = std::fread(g_rom.data(), 1, g_rom.size(), f);
	std::fclose(f);
	if (got != g_rom.size())
	{
		std::fprintf(stderr, "%s: expected 32768 bytes\n", path);
		return 1;
	}

	for (auto &t : tables)
		for (int n = 0; n < t.second; n++)
		{
			const size_t at = size_t(t.first + 2 * n) & 0x7fff;
			const u16 target = u16(g_rom[at] << 8 | g_rom[(at + 1) & 0x7fff]);
			char name[32];
			std::snprintf(name, sizeof(name), "table%04X_%d", t.first, n);
			labels[target] = name;
			work.push_back(target);
		}
	for (int v = 0; v < 0x20; v++)
	{
		const u16 t = u16(g_rom[size_t(2 * v)] << 8 | g_rom[size_t(2 * v + 1)]);
		char name[32];
		if ((v & 0x0f) == 0)
			std::snprintf(name, sizeof(name), "cpu%d_reset", v >> 4);
		else if ((v & 0x0f) == 1)
			std::snprintf(name, sizeof(name), "cpu%d_pi", v >> 4);
		else
			std::snprintf(name, sizeof(name), "cpu%d_irq%d", v >> 4, (v & 0x0f) - 2);
		if (!labels.count(t))
			labels[t] = name;
		else
			labels[t] += std::string(" ") + name;
		work.push_back(t);
	}

	// line start -> (text, length); covered bytes
	std::map<u16, Insn> lines;
	std::vector<bool> covered(0x8000, false);
	while (!work.empty())
	{
		u16 pc = work.front();
		work.pop_front();
		while (pc < 0x8000 && !lines.count(pc) && !covered[pc])
		{
			Insn insn = decode(pc);
			const u8 op = g_rom[pc];
			if (op >= 0x01 && op <= 0x0f && pc + 1 < 0x8000)
			{
				// REP n: the repeated instruction and its n extra operand sets, each a line
				lines[pc] = insn;
				covered[pc] = true;
				const u16 at = u16(pc + 1);
				Insn first = decode(at);
				const int sets = op;
				const int operands = first.length - 1;
				std::vector<std::pair<u16, Insn>> parts{{at, first}};
				u16 p = u16(at + first.length);
				for (int s = 0; s < sets && operands > 0; s++)
				{
					Insn extra = decode(p, g_rom[at]);
					extra.text = ".. " + extra.text;
					parts.push_back({p, extra});
					p = u16(p + operands);
				}
				bool ends = false;
				for (auto &part : parts)
				{
					lines[part.first] = part.second;
					for (int b = 0; b < part.second.length && part.first + b < 0x8000; b++)
						covered[size_t(part.first + b)] = true;
					for (u16 t : part.second.targets)
					{
						if (!labels.count(t))
							labels[t] = part.second.call ? "sub" : "L";
						work.push_back(t);
					}
					ends = ends || part.second.ends;
				}
				if (ends)
					break;
				pc = p;
				continue;
			}
			lines[pc] = insn;
			for (int b = 0; b < insn.length && pc + b < 0x8000; b++)
				covered[size_t(pc + b)] = true;
			for (u16 t : insn.targets)
			{
				if (!labels.count(t) || labels[t] == "L")
					labels[t] = insn.call ? "sub" : (labels.count(t) ? labels[t] : "L");
				work.push_back(t);
			}
			if (insn.ends)
				break;
			pc = u16(pc + insn.length);
		}
	}

	// listing
	size_t code_bytes = 0;
	for (bool c : covered)
		code_bytes += c;
	std::printf("; bsrackdis: %s, %zu bytes reached as code\n", path, code_bytes);
	for (u32 pc = 0; pc < 0x8000;)
	{
		auto line = lines.find(u16(pc));
		if (line != lines.end())
		{
			auto l = labels.find(u16(pc));
			if (l != labels.end())
				std::printf("\n%s_%04X:\n", l->second.c_str(), pc);
			const Insn &insn = line->second;
			std::printf("%04X ", pc);
			for (int i = 0; i < 6; i++)
				std::printf(i < insn.length ? " %02X" : "   ", g_rom[(pc + u32(i)) & 0x7fff]);
			std::printf("  %s\n", insn.text.c_str());
			pc += u32(insn.length);
			continue;
		}
		if (covered[pc])
		{
			pc++;
			continue;
		}
		// data until the next code
		u32 end = pc;
		while (end < 0x8000 && !covered[end] && !lines.count(u16(end)))
			end++;
		for (u32 a = pc; a < end; a += 16)
		{
			std::printf("%04X  db", a);
			for (u32 b = a; b < end && b < a + 16; b++)
				std::printf(" %02X", g_rom[b]);
			std::printf("\n");
		}
		pc = end;
	}
	return 0;
}
