#!/usr/bin/env python3
"""MCS-48 (8048/8049, 80C49) disassembler, after MAME's mcs48dsm.cpp.

Usage: dis49.py ROM [start] [end]        linear listing (addresses in hex)
       dis49.py -t ROM [start] [end]     trace code from the vectors 000/003/007
                                         (-x: from 000 only) and any extra hex
                                         entry points given with -e ADDR;
                                         unreached bytes are printed as DB
       dis49.py --check mcs48dsm.cpp     compare all 256 opcodes with MAME

Prints address, bytes, mnemonic and, for port, BUS, MOVX, expander, timer,
interrupt, test-pin, bank and register-bank instructions, a comment saying what
the instruction touches on the chip's pins. Conditional jumps and DJNZ stay in
the page of their operand byte, as on the chip and in MAME. JMP/CALL take A11
from the SEL MB0/MB1 flag; the listing shows 11-bit targets and -t follows them
in the current 2K bank (MB1 would select external program memory, flagged).
"""
import re
import sys

# name, operand kind; kinds: "" none, "n" immediate, "j" 11-bit JMP/CALL,
# "c" conditional jump in the operand's page
T = {}
for i, r in enumerate(["@R0", "@R1"]):
    T[0x10 + i] = (f"INC {r}", ""); T[0x20 + i] = (f"XCH A,{r}", "")
    T[0x30 + i] = (f"XCHD A,{r}", ""); T[0x40 + i] = (f"ORL A,{r}", "")
    T[0x50 + i] = (f"ANL A,{r}", ""); T[0x60 + i] = (f"ADD A,{r}", "")
    T[0x70 + i] = (f"ADDC A,{r}", ""); T[0x80 + i] = (f"MOVX A,{r}", "")
    T[0x90 + i] = (f"MOVX {r},A", ""); T[0xA0 + i] = (f"MOV {r},A", "")
    T[0xB0 + i] = (f"MOV {r},#", "n"); T[0xD0 + i] = (f"XRL A,{r}", "")
    T[0xF0 + i] = (f"MOV A,{r}", "")
for i in range(8):
    r = f"R{i}"
    T[0x18 + i] = (f"INC {r}", ""); T[0x28 + i] = (f"XCH A,{r}", "")
    T[0x48 + i] = (f"ORL A,{r}", ""); T[0x58 + i] = (f"ANL A,{r}", "")
    T[0x68 + i] = (f"ADD A,{r}", ""); T[0x78 + i] = (f"ADDC A,{r}", "")
    T[0xA8 + i] = (f"MOV {r},A", ""); T[0xB8 + i] = (f"MOV {r},#", "n")
    T[0xC8 + i] = (f"DEC {r}", ""); T[0xD8 + i] = (f"XRL A,{r}", "")
    T[0xE8 + i] = (f"DJNZ {r},", "c"); T[0xF8 + i] = (f"MOV A,{r}", "")
    T[0x04 + 0x20 * i] = ("JMP ", "j"); T[0x14 + 0x20 * i] = ("CALL ", "j")
    T[0x12 + 0x20 * i] = (f"JB{i} ", "c")
for i, p in enumerate("4567"):
    T[0x0C + i] = (f"MOVD A,P{p}", ""); T[0x3C + i] = (f"MOVD P{p},A", "")
    T[0x8C + i] = (f"ORLD P{p},A", ""); T[0x9C + i] = (f"ANLD P{p},A", "")
T.update({
    0x00: ("NOP", ""), 0x01: ("IDL", ""), 0x02: ("OUTL BUS,A", ""), 0x03: ("ADD A,#", "n"),
    0x05: ("EN I", ""), 0x07: ("DEC A", ""), 0x08: ("INS A,BUS", ""), 0x09: ("IN A,P1", ""),
    0x0A: ("IN A,P2", ""), 0x13: ("ADDC A,#", "n"), 0x15: ("DIS I", ""), 0x16: ("JTF ", "c"),
    0x17: ("INC A", ""), 0x23: ("MOV A,#", "n"), 0x25: ("EN TCNTI", ""), 0x26: ("JNT0 ", "c"),
    0x27: ("CLR A", ""), 0x35: ("DIS TCNTI", ""), 0x36: ("JT0 ", "c"), 0x37: ("CPL A", ""),
    0x39: ("OUTL P1,A", ""), 0x3A: ("OUTL P2,A", ""), 0x42: ("MOV A,T", ""),
    0x43: ("ORL A,#", "n"), 0x45: ("STRT CNT", ""), 0x46: ("JNT1 ", "c"), 0x47: ("SWAP A", ""),
    0x53: ("ANL A,#", "n"), 0x55: ("STRT T", ""), 0x56: ("JT1 ", "c"), 0x57: ("DA A", ""),
    0x62: ("MOV T,A", ""), 0x65: ("STOP TCNT", ""), 0x67: ("RRC A", ""), 0x75: ("ENT0 CLK", ""),
    0x76: ("JF1 ", "c"), 0x77: ("RR A", ""), 0x83: ("RET", ""), 0x85: ("CLR F0", ""),
    0x86: ("JNI ", "c"), 0x88: ("ORL BUS,#", "n"), 0x89: ("ORL P1,#", "n"),
    0x8A: ("ORL P2,#", "n"), 0x93: ("RETR", ""), 0x95: ("CPL F0", ""), 0x96: ("JNZ ", "c"),
    0x97: ("CLR C", ""), 0x98: ("ANL BUS,#", "n"), 0x99: ("ANL P1,#", "n"),
    0x9A: ("ANL P2,#", "n"), 0xA3: ("MOVP A,@A", ""), 0xA5: ("CLR F1", ""), 0xA7: ("CPL C", ""),
    0xB3: ("JMPP @A", ""), 0xB5: ("CPL F1", ""), 0xB6: ("JF0 ", "c"), 0xC5: ("SEL RB0", ""),
    0xC6: ("JZ ", "c"), 0xC7: ("MOV A,PSW", ""), 0xD3: ("XRL A,#", "n"), 0xD5: ("SEL RB1", ""),
    0xD7: ("MOV PSW,A", ""), 0xE3: ("MOVP3 A,@A", ""), 0xE5: ("SEL MB0", ""), 0xE6: ("JNC ", "c"),
    0xE7: ("RL A", ""), 0xF5: ("SEL MB1", ""), 0xF6: ("JC ", "c"), 0xF7: ("RLC A", ""),
})
TABLE = [T.get(op, ("ILLEGAL", "")) for op in range(256)]

NOTE = {
    "IDL": "CMOS HALT; MAME treats 01 as illegal",
    "OUTL BUS,A": "BUS write: DB0-7 latched, /WR strobe",
    "INS A,BUS": "BUS read: DB0-7 with /RD strobe",
    "ORL BUS,#": "BUS read-modify-write (latched DB0-7)", "ANL BUS,#": "BUS read-modify-write (latched DB0-7)",
    "MOVX A,@R0": "ext data read: ALE, addr R0 on DB, /RD", "MOVX A,@R1": "ext data read: ALE, addr R1 on DB, /RD",
    "MOVX @R0,A": "ext data write: ALE, addr R0 on DB, /WR", "MOVX @R1,A": "ext data write: ALE, addr R1 on DB, /WR",
    "IN A,P1": "port 1 read (quasi-bidirectional, ANDed with latch)",
    "IN A,P2": "port 2 read (quasi-bidirectional, ANDed with latch)",
    "OUTL P1,A": "port 1 write", "OUTL P2,A": "port 2 write",
    "EN I": "enable /INT", "DIS I": "disable /INT", "JNI ": "jump if /INT low",
    "EN TCNTI": "enable timer/counter interrupt (vector 007)", "DIS TCNTI": "disable timer/counter interrupt",
    "STRT T": "timer start: +1 every 32 machine cycles", "STRT CNT": "counter start: T1 falling edges",
    "STOP TCNT": "timer/counter stop", "MOV A,T": "read timer", "MOV T,A": "load timer",
    "JTF ": "timer overflow flag (clears it)", "ENT0 CLK": "T0 outputs clock/3",
    "JT0 ": "T0 pin", "JNT0 ": "T0 pin", "JT1 ": "T1 pin", "JNT1 ": "T1 pin",
    "SEL MB0": "A11 = 0 for next JMP/CALL", "SEL MB1": "A11 = 1 for next JMP/CALL: external program memory",
    "SEL RB0": "register bank 0 (RAM 00-07)", "SEL RB1": "register bank 1 (RAM 18-1F)",
    "RETR": "return, restore PSW, end of interrupt", "MOV PSW,A": "PSW write (bank, stack, flags)",
    "MOVP A,@A": "table read, current page", "MOVP3 A,@A": "table read, page 3", "JMPP @A": "jump via table in current page",
}
EXPANDER = "8243 expander: nibble on P20-23, PROG strobe"


def disasm(rom, pc):
    """Return (size, text, comment, kind, target)."""
    op = rom[pc]
    name, kind = TABLE[op]
    size, target, text = 1, None, name
    if kind:
        arg = rom[(pc + 1) % len(rom)]
        size = 2
        if kind == "n":
            text += f"${arg:02X}"
        elif kind == "j":
            target = ((op >> 5) << 8) | arg
            text += f"${target:03X}"
        else:
            target = ((pc + 1) & 0x700) | arg
            text += f"${target:03X}"
    note = NOTE.get(name, "")
    if name.startswith(("MOVD", "ORLD", "ANLD")):
        note = EXPANDER
    if kind == "n" and re.match(r"(ORL|ANL) (P1|P2|BUS),#", name):
        port = name.split()[1].split(",")[0]
        bits = arg if name.startswith("ORL") else ~arg & 0xFF
        val = 1 if name.startswith("ORL") else 0
        sel = [str(b) for b in range(8) if bits >> b & 1]
        lab = {"P1": "P1", "P2": "P2", "BUS": "DB"}[port]
        note = f"{lab}." + ",".join(sel) + f" <- {val}" if sel else f"{lab} unchanged"
    return size, text, note, kind, target


def check(path):
    """Compare all 256 opcodes with the switch in MAME's mcs48dsm.cpp, taking
    the plain MCS-48 branches (m_upi41 = m_i802x = false)."""
    src = re.sub(r"//[^\n]*", "", open(path).read())
    src = re.sub(r'm_i802x \? "[^"]*" : ', "", src)
    mame = {}
    for m in re.finditer(r"case 0x([0-9a-f]{2}):(.*?)(?=case 0x|default:)", src, re.S):
        toks = re.findall(r'"[^"]*"|stream_format|if|else|\(|\)|\{|\}|;|!|&&|m_i802x|m_upi41|[^\s(){};!&"]+', m.group(2))
        out, i = [], [0]

        def cond():
            i[0] += 1                        # "("
            val, neg, depth = True, False, 1
            while depth:
                t = toks[i[0]]; i[0] += 1
                if t == "(": depth += 1
                elif t == ")": depth -= 1
                elif t == "!": neg = True
                elif t in ("m_i802x", "m_upi41"): val = val and neg; neg = False
            return val

        def stmt(live):
            t = toks[i[0]]
            if t == "{":
                i[0] += 1
                while toks[i[0]] != "}":
                    stmt(live)
                i[0] += 1
            elif t == "if":
                i[0] += 1
                c = cond()
                stmt(live and c)
                if i[0] < len(toks) and toks[i[0]] == "else":
                    i[0] += 1
                    stmt(live and not c)
            else:
                while toks[i[0]] != ";":
                    if toks[i[0]].endswith("stream_format") and live:
                        out.append(next(x for x in toks[i[0]:] if x.startswith('"'))[1:-1])
                    i[0] += 1
                i[0] += 1

        while i[0] < len(toks):
            stmt(True)
        mame[int(m.group(1), 16)] = out[0] if out else "illegal"
    bad = 0
    for op in range(256):
        fmt = mame.get(op, "illegal")
        rom = bytes([op, 0x5A] + [0] * 0x7FE)
        size, text, *_ = disasm(rom, 0)
        exp = fmt.replace("%02X", "5A").replace("%03X", "05A")
        nsize = 2 if "%" in fmt else 1
        norm = lambda x: re.sub(r"\s+", " ", x.strip().lower())
        if norm(text) != norm(exp) or size != nsize:
            bad += 1
            print(f"{op:02X}: dis49 '{text}' ({size})  MAME '{exp}' ({nsize})")
    print("all 256 opcodes match MAME" if not bad else f"{bad} mismatches")
    return bad


def trace(rom, entries):
    """Follow control flow from the entry points. Returns {addr: size} of code."""
    code, todo, notes = {}, list(entries), []
    while todo:
        pc = todo.pop()
        while 0 <= pc < len(rom) and pc not in code:
            size, text, _, kind, target = disasm(rom, pc)
            code[pc] = size
            name = TABLE[rom[pc]][0]
            if name == "ILLEGAL":
                notes.append(f"{pc:03X}: illegal opcode {rom[pc]:02X} on a traced path")
            if name == "SEL MB1":
                notes.append(f"{pc:03X}: SEL MB1 (external program memory)")
            if kind in ("c", "j") and target is not None:
                todo.append(target)
            if name in ("RET", "RETR", "JMPP @A") or name.startswith("JMP "):
                if name == "JMPP @A":
                    notes.append(f"{pc:03X}: JMPP @A, table in page {pc >> 8:X} not followed")
                break
            pc += size
    return code, notes


def main():
    args = sys.argv[1:]
    if args and args[0] == "--check":
        sys.exit(1 if check(args[1]) else 0)
    tracing = False
    vectors = [0x000, 0x003, 0x007]
    extra = []
    while args and args[0].startswith("-"):
        a = args.pop(0)
        if a == "-t":
            tracing = True
        elif a == "-x":
            vectors = [0x000]
        elif a == "-e":
            extra.append(int(args.pop(0), 16))
    rom = open(args[0], "rb").read()
    start = int(args[1], 16) if len(args) > 1 else 0
    end = int(args[2], 16) if len(args) > 2 else len(rom)
    code = None
    if tracing:
        code, notes = trace(rom, vectors + extra)
        for n in notes:
            print("; " + n)
        print(f"; {sum(code.values())} of {len(rom)} bytes reached as code")
    pc = start
    while pc < end:
        if code is not None and pc not in code:
            run = pc
            while run < end and run not in code:
                run += 1
            data = rom[pc:run]
            for i in range(0, len(data), 16):
                print(f"{pc + i:03X}  DB " + " ".join(f"{b:02X}" for b in data[i:i + 16]))
            pc = run
            continue
        size, text, note, *_ = disasm(rom, pc)
        b = " ".join(f"{rom[(pc + i) % len(rom)]:02X}" for i in range(size))
        line = f"{pc:03X}  {b:6s} {text}"
        print(f"{line:30s} ; {note}" if note else line)
        pc += size


if __name__ == "__main__":
    main()
