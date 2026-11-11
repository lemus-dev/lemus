#!/usr/bin/env python3
"""A small 8051 disassembler for reading the Virus C firmware, with the 80C515's SFR names.

    dasm51.py <rom> <bank> <start> <end>     addresses as the 80C515 sees them ($8000+ in the bank)
"""
import sys

SFR = {0x80: 'P0', 0x81: 'SP', 0x82: 'DPL', 0x83: 'DPH', 0x87: 'PCON', 0x88: 'TCON', 0x89: 'TMOD', 0x8a: 'TL0',
       0x8b: 'TL1', 0x8c: 'TH0', 0x8d: 'TH1', 0x90: 'P1', 0x98: 'SCON', 0x99: 'SBUF', 0xa0: 'P2', 0xa8: 'IEN0',
       0xa9: 'IP0', 0xb0: 'P3', 0xb8: 'IEN1', 0xb9: 'IP1', 0xc0: 'IRCON', 0xc1: 'CCEN', 0xc8: 'T2CON', 0xca: 'CRCL',
       0xcb: 'CRCH', 0xcc: 'TL2', 0xcd: 'TH2', 0xd0: 'PSW', 0xd8: 'ADCON', 0xd9: 'ADDAT', 0xda: 'DAPR', 0xdb: 'P6',
       0xe0: 'A', 0xe8: 'P4', 0xf0: 'B', 0xf8: 'P5'}

def direct(v):
    return SFR.get(v, '%02Xh' % v) if v >= 0x80 else '%02Xh' % v

def bit(v):
    if v < 0x80:
        return '%02Xh.%d' % (0x20 + (v >> 3), v & 7)
    return '%s.%d' % (SFR.get(v & 0xf8, '%02Xh' % (v & 0xf8)), v & 7)

# (mnemonic, operand pattern, length). Patterns: d direct, i immediate, b bit, r rel, a11, a16, d16
T = {0x00: ('NOP', '', 1), 0x02: ('LJMP', 'a16', 3), 0x03: ('RR', 'A', 1), 0x04: ('INC', 'A', 1), 0x05: ('INC', 'd', 2),
     0x06: ('INC', '@R0', 1), 0x07: ('INC', '@R1', 1), 0x10: ('JBC', 'b,r', 3), 0x12: ('LCALL', 'a16', 3),
     0x13: ('RRC', 'A', 1), 0x14: ('DEC', 'A', 1), 0x15: ('DEC', 'd', 2), 0x16: ('DEC', '@R0', 1), 0x17: ('DEC', '@R1', 1),
     0x20: ('JB', 'b,r', 3), 0x22: ('RET', '', 1), 0x23: ('RL', 'A', 1), 0x24: ('ADD', 'A,#i', 2), 0x25: ('ADD', 'A,d', 2),
     0x26: ('ADD', 'A,@R0', 1), 0x27: ('ADD', 'A,@R1', 1), 0x30: ('JNB', 'b,r', 3), 0x32: ('RETI', '', 1),
     0x33: ('RLC', 'A', 1), 0x34: ('ADDC', 'A,#i', 2), 0x35: ('ADDC', 'A,d', 2), 0x36: ('ADDC', 'A,@R0', 1),
     0x37: ('ADDC', 'A,@R1', 1), 0x40: ('JC', 'r', 2), 0x42: ('ORL', 'd,A', 2), 0x43: ('ORL', 'd,#i', 3),
     0x44: ('ORL', 'A,#i', 2), 0x45: ('ORL', 'A,d', 2), 0x46: ('ORL', 'A,@R0', 1), 0x47: ('ORL', 'A,@R1', 1),
     0x50: ('JNC', 'r', 2), 0x52: ('ANL', 'd,A', 2), 0x53: ('ANL', 'd,#i', 3), 0x54: ('ANL', 'A,#i', 2),
     0x55: ('ANL', 'A,d', 2), 0x56: ('ANL', 'A,@R0', 1), 0x57: ('ANL', 'A,@R1', 1), 0x60: ('JZ', 'r', 2),
     0x62: ('XRL', 'd,A', 2), 0x63: ('XRL', 'd,#i', 3), 0x64: ('XRL', 'A,#i', 2), 0x65: ('XRL', 'A,d', 2),
     0x66: ('XRL', 'A,@R0', 1), 0x67: ('XRL', 'A,@R1', 1), 0x70: ('JNZ', 'r', 2), 0x72: ('ORL', 'C,b', 2),
     0x73: ('JMP', '@A+DPTR', 1), 0x74: ('MOV', 'A,#i', 2), 0x75: ('MOV', 'd,#i', 3), 0x76: ('MOV', '@R0,#i', 2),
     0x77: ('MOV', '@R1,#i', 2), 0x80: ('SJMP', 'r', 2), 0x82: ('ANL', 'C,b', 2), 0x83: ('MOVC', 'A,@A+PC', 1),
     0x84: ('DIV', 'AB', 1), 0x85: ('MOV', 'dd', 3), 0x86: ('MOV', 'd,@R0', 2), 0x87: ('MOV', 'd,@R1', 2),
     0x90: ('MOV', 'DPTR,#d16', 3), 0x92: ('MOV', 'b,C', 2), 0x93: ('MOVC', 'A,@A+DPTR', 1), 0x94: ('SUBB', 'A,#i', 2),
     0x95: ('SUBB', 'A,d', 2), 0x96: ('SUBB', 'A,@R0', 1), 0x97: ('SUBB', 'A,@R1', 1), 0xa0: ('ORL', 'C,/b', 2),
     0xa2: ('MOV', 'C,b', 2), 0xa3: ('INC', 'DPTR', 1), 0xa4: ('MUL', 'AB', 1), 0xa6: ('MOV', '@R0,d', 2),
     0xa7: ('MOV', '@R1,d', 2), 0xb0: ('ANL', 'C,/b', 2), 0xb2: ('CPL', 'b', 2), 0xb3: ('CPL', 'C', 1),
     0xb4: ('CJNE', 'A,#i,r', 3), 0xb5: ('CJNE', 'A,d,r', 3), 0xb6: ('CJNE', '@R0,#i,r', 3), 0xb7: ('CJNE', '@R1,#i,r', 3),
     0xc0: ('PUSH', 'd', 2), 0xc2: ('CLR', 'b', 2), 0xc3: ('CLR', 'C', 1), 0xc4: ('SWAP', 'A', 1), 0xc5: ('XCH', 'A,d', 2),
     0xc6: ('XCH', 'A,@R0', 1), 0xc7: ('XCH', 'A,@R1', 1), 0xd0: ('POP', 'd', 2), 0xd2: ('SETB', 'b', 2),
     0xd3: ('SETB', 'C', 1), 0xd4: ('DA', 'A', 1), 0xd5: ('DJNZ', 'd,r', 3), 0xd6: ('XCHD', 'A,@R0', 1),
     0xd7: ('XCHD', 'A,@R1', 1), 0xe0: ('MOVX', 'A,@DPTR', 1), 0xe2: ('MOVX', 'A,@R0', 1), 0xe3: ('MOVX', 'A,@R1', 1),
     0xe4: ('CLR', 'A', 1), 0xe5: ('MOV', 'A,d', 2), 0xe6: ('MOV', 'A,@R0', 1), 0xe7: ('MOV', 'A,@R1', 1),
     0xf0: ('MOVX', '@DPTR,A', 1), 0xf2: ('MOVX', '@R0,A', 1), 0xf3: ('MOVX', '@R1,A', 1), 0xf4: ('CPL', 'A', 1),
     0xf5: ('MOV', 'd,A', 2), 0xf6: ('MOV', '@R0,A', 1), 0xf7: ('MOV', '@R1,A', 1)}
for n in range(8):
    T[0x08 + n] = ('INC', 'R%d' % n, 1); T[0x18 + n] = ('DEC', 'R%d' % n, 1); T[0x28 + n] = ('ADD', 'A,R%d' % n, 1)
    T[0x38 + n] = ('ADDC', 'A,R%d' % n, 1); T[0x48 + n] = ('ORL', 'A,R%d' % n, 1); T[0x58 + n] = ('ANL', 'A,R%d' % n, 1)
    T[0x68 + n] = ('XRL', 'A,R%d' % n, 1); T[0x78 + n] = ('MOV', 'R%d,#i' % n, 2); T[0x88 + n] = ('MOV', 'd,R%d' % n, 2)
    T[0x98 + n] = ('SUBB', 'A,R%d' % n, 1); T[0xa8 + n] = ('MOV', 'R%d,d' % n, 2); T[0xb8 + n] = ('CJNE', 'R%d,#i,r' % n, 3)
    T[0xc8 + n] = ('XCH', 'A,R%d' % n, 1); T[0xd8 + n] = ('DJNZ', 'R%d,r' % n, 2); T[0xe8 + n] = ('MOV', 'A,R%d' % n, 1)
    T[0xf8 + n] = ('MOV', 'R%d,A' % n, 1)
for n in range(8):
    T[0x01 + n * 0x20] = ('AJMP', 'a11', 2); T[0x11 + n * 0x20] = ('ACALL', 'a11', 2)

def dis(code, pc, read):
    op = read(pc)
    if op not in T:
        return 1, 'DB %02Xh' % op
    m, pat, n = T[op]
    b = [read(pc + k) for k in range(1, n)]
    if pat == 'a16': s = '%04Xh' % (b[0] << 8 | b[1])
    elif pat == 'a11': s = '%04Xh' % (((pc + 2) & 0xf800) | ((op >> 5) << 8) | b[0])
    elif pat == 'r': s = '%04Xh' % ((pc + 2 + (b[0] - 256 if b[0] > 127 else b[0])) & 0xffff)
    elif pat == 'dd': s = '%s,%s' % (direct(b[1]), direct(b[0]))
    else:
        s = pat; vals = list(b)
        if 'b,r' in s or s.startswith('b') or ',b' in s or '/b' in s:
            s = s.replace('b', bit(vals.pop(0)), 1) if not s.startswith('A') else s
        if 'd16' in s: s = s.replace('#d16', '#%04Xh' % (vals[0] << 8 | vals[1])); vals = []
        if s.startswith('d') or ',d' in s:
            s = s.replace('d', direct(vals.pop(0)), 1) if s.startswith('d') else s.replace(',d', ',' + direct(vals.pop(0)), 1)
        if '#i' in s: s = s.replace('#i', '#%02Xh' % vals.pop(0))
        if s.endswith(',r') or s == 'r':
            rel = vals.pop(0); t = (pc + n + (rel - 256 if rel > 127 else rel)) & 0xffff
            s = s[:-1] + '%04Xh' % t
    return n, '%-6s %s' % (m, s)

if __name__ == '__main__':
    rom = open(sys.argv[1], 'rb').read()
    bank, pc, end = int(sys.argv[2], 0), int(sys.argv[3], 16), int(sys.argv[4], 16)
    read = lambda a: rom[a] if a < 0x8000 else rom[bank * 0x8000 + a - 0x8000]
    while pc < end:
        n, text = dis(rom, pc, read)
        print('%04X  %-12s %s' % (pc, ' '.join('%02X' % read(pc + k) for k in range(n)), text))
        pc += n
