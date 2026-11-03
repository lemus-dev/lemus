// dis7810: disassembles a uPD7810 ROM image with MAME's disassembler (BSD-3-Clause).
// Usage: dis7810 ROM [START [END]] [--base ADDR]   (addresses in hex)
#include "emu.h"
#include "upd7810_dasm.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>

int main(int argc, char **argv)
{
	if (argc < 2) { std::fprintf(stderr, "usage: dis7810 ROM [START [END]] [--base ADDR]\n"); return 1; }
	std::vector<u8> mem(0x10000, 0xff);
	unsigned base = 0, start = 0, end = 0;
	std::vector<const char *> pos;
	for (int i = 2; i < argc; i++)
		if (!std::strcmp(argv[i], "--base") && i + 1 < argc) base = std::strtoul(argv[++i], nullptr, 16);
		else pos.push_back(argv[i]);
	FILE *f = std::fopen(argv[1], "rb");
	if (!f) { std::perror(argv[1]); return 1; }
	size_t n = std::fread(mem.data() + base, 1, 0x10000 - base, f);
	std::fclose(f);
	start = pos.size() > 0 ? std::strtoul(pos[0], nullptr, 16) : base;
	end = pos.size() > 1 ? std::strtoul(pos[1], nullptr, 16) : base + (unsigned)n;
	upd7810_disassembler d;
	util::disasm_interface::data_buffer buf(mem.data());
	for (unsigned pc = start; pc < end;)
	{
		std::ostringstream s;
		offs_t r = d.disassemble(s, pc, buf, buf);
		unsigned len = r & 0xffff;
		if (!len) len = 1;
		std::printf("%04X: ", pc);
		for (unsigned k = 0; k < 4; k++)
			if (k < len) std::printf("%02X ", mem[(pc + k) & 0xffff]); else std::printf("   ");
		std::printf(" %s\n", s.str().c_str());
		pc += len;
	}
}
