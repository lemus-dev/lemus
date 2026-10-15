#!/usr/bin/env python3
"""uCOM-43 (uPD650) disassembler, after MAME's ucom4d.cpp.

Usage: dis650.py ROM [start] [end]   (addresses in hex)
Prints address, bytes, mnemonic and, for port instructions, the port letter.
"""
import sys

M = ("NOP DI S TIT TC TTM DAA TAL AD ADS DAS CLC CM INC OP DEC "
     "CMA CIA TLA DED STM LDI CLI CI EXL ADC XC STC ? INM OCD DEM "
     "FBF FBF FBF FBF TAB TAB TAB TAB X XM XM XM XD XMD XMD XMD "
     "RAR EI IP IND CMB CMB CMB CMB L LM LM LM XI XMI XMI XMI "
     "IA JPA TAZ TAW OE ? TLY THX RT RTS XAZ XAW XLS XHR XLY XHX "
     "TPB TPB TPB TPB TPA TPA TPA TPA TMB TMB TMB TMB FBT FBT FBT FBT "
     "RPB RPB RPB RPB REB REB REB REB RMB RMB RMB RMB RFB RFB RFB RFB "
     "SPB SPB SPB SPB SEB SEB SEB SEB SMB SMB SMB SMB SFB SFB SFB SFB").split()
TABLE = M + ["LDZ"] * 16 + ["CLA"] + ["LI"] * 15 + ["JMP"] * 8 + ["CAL"] * 8 + ["CZP"] * 16 + ["JCP"] * 64
BITS = {"LI": 4, "LM": 2, "LDI": 80, "LDZ": 4, "XM": 2, "XMI": 2, "XMD": 2, "RMB": 2, "SMB": 2,
        "REB": 2, "SEB": 2, "RPB": 2, "SPB": 2, "JMP": 83, "JCP": 6, "CAL": 83, "CZP": 4,
        "CI": 40, "CMB": 2, "TAB": 2, "CLI": 40, "TMB": 2, "TPA": 2, "TPB": 2, "OCD": 80,
        "SFB": 2, "RFB": 2, "FBT": 2, "FBF": 2, "STM": 80}


def disasm(rom, pc):
    op = rom[pc]
    name = TABLE[op]
    bits = BITS.get(name, 0)
    size = 1
    text = name
    if bits:
        param = op & ((1 << (bits % 10)) - 1)
        if bits // 10:
            op2 = rom[(pc & ~0xff) | ((pc + 1) & 0xff)]
            size = 2
            param = (param << (bits // 10)) | (op2 & ((1 << (bits // 10)) - 1))
            bits = bits % 10 + bits // 10
        if name == "CZP":
            param <<= 2; bits += 2
        if name == "JCP":
            param = (pc & ~0x3f) | param
            text += f" ${param:03X}"
        elif bits <= 4:
            text += f" {param}"
        elif bits <= 8:
            text += f" ${param:02X}"
        else:
            text += f" ${param:03X}"
    return size, text


def main():
    rom = open(sys.argv[1], "rb").read()
    start = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0
    end = int(sys.argv[3], 16) if len(sys.argv) > 3 else len(rom)
    pc = start
    while pc < end:
        size, text = disasm(rom, pc)
        b = " ".join(f"{rom[pc + i]:02X}" for i in range(size))
        print(f"{pc:03X}  {b:6s} {text}")
        pc += size


if __name__ == "__main__":
    main()
