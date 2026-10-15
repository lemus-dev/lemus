import ctypes, os
_L = ctypes.CDLL(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'libprobe.dylib'))
ROM = open(os.path.expanduser('~/src/lemus/private/roms/tb303/d650c-133.ic8'), 'rb').read()
class Ev(ctypes.Structure):
    _fields_ = [('t', ctypes.c_double), ('type', ctypes.c_int), ('a', ctypes.c_int), ('b', ctypes.c_int), ('c', ctypes.c_int)]
P = ctypes.c_void_p
for n, a, r in [('p_new', [ctypes.c_char_p], P), ('p_power', [P], None), ('p_key', [P, ctypes.c_int, ctypes.c_int], None),
                ('p_mode', [P, ctypes.c_int], None), ('p_track', [P, ctypes.c_int], None), ('p_run', [P, ctypes.c_int], None),
                ('p_clock', [P, ctypes.c_int], None), ('p_clock_auto', [P, ctypes.c_double, ctypes.c_double, ctypes.c_double], None),
                ('p_log_int', [P, ctypes.c_int], None), ('p_time', [P], ctypes.c_double), ('p_irq_phase', [P], ctypes.c_double),
                ('p_advance', [P, ctypes.c_double], None), ('p_events', [P, ctypes.c_void_p, ctypes.c_int], ctypes.c_int),
                ('p_leds', [P, ctypes.c_void_p], None), ('p_ram', [P, ctypes.c_void_p], None), ('p_setram', [P, ctypes.c_char_p], None),
                ('p_dac', [P], ctypes.c_int), ('p_pins', [P], ctypes.c_int)]:
    f = getattr(_L, n); f.argtypes = a; f.restype = r
KEYS = "C C# D D# E F F# G G# A A# B C2 DOWN UP ACCENT SLIDE CLEAR FUNCTION PITCH TIME BACK TAP".split()
LEDS = "C C# D D# E F F# G G# A A# B C2 DOWN UP ACCENT SLIDE PITCH TIME FUNCTION RUN".split()
MAT = ["C","D","E","F","G","A","B","C2","DOWN","UP","ACCENT","SLIDE","C#","D#","F#","G#"]  # matrix bit order col*4+row
MODES = {'tw': 0, 'tp': 1, 'pp': 2, 'pw': 3}
DIGIT = {1:'C',2:'D',3:'E',4:'F',5:'G',6:'A',7:'B',8:'C2',9:'DOWN',0:'UP',100:'ACCENT',200:'SLIDE'}
PAT = {1:'C',2:'D',3:'E',4:'F',5:'G',6:'A',7:'B',8:'C2'}

class TB:
    def __init__(self, ram=None, mode='pp', track=1):
        self.p = _L.p_new(ROM)
        if ram is not None: self.setram(ram)
        self.mode(mode); self.track(track)
        _L.p_power(self.p)
    def mode(self, m): _L.p_mode(self.p, MODES[m])
    def track(self, n): _L.p_track(self.p, n - 1)
    def t(self): return _L.p_time(self.p)
    def wait(self, ms): _L.p_advance(self.p, ms / 1000.0)
    def hold(self, k): _L.p_key(self.p, KEYS.index(k), 1)
    def release(self, k): _L.p_key(self.p, KEYS.index(k), 0)
    def press(self, k, down=50, up=50):
        self.hold(k); self.wait(down); self.release(k); self.wait(up)
    def run(self, on=True, bpm=120, duty=0.5, offset=0.005, clock=True):
        _L.p_run(self.p, 1 if on else 0)
        if on and clock: _L.p_clock_auto(self.p, bpm, duty, offset)
        if not on and clock: _L.p_clock(self.p, 0)
    def run_level(self, on): _L.p_run(self.p, 1 if on else 0)
    def clock(self, level): _L.p_clock(self.p, 1 if level else 0)
    def log_int(self, on=True): _L.p_log_int(self.p, 1 if on else 0)
    def irq_phase(self): return _L.p_irq_phase(self.p)
    def events(self):
        out = []
        buf = (Ev * 4096)()
        while True:
            n = _L.p_events(self.p, buf, 4096)
            out += [(buf[i].t, buf[i].type, buf[i].a, buf[i].b) for i in range(n)]
            if n < 4096: return out
    def ram(self):
        b = (ctypes.c_uint8 * 3072)(); _L.p_ram(self.p, b); return bytes(b)
    def setram(self, data): _L.p_setram(self.p, bytes(data))
    def leds(self):
        b = (ctypes.c_float * 41)(); _L.p_leds(self.p, b); return list(b)
    def dac(self): return _L.p_dac(self.p)
    def pins(self): return _L.p_pins(self.p)
    def bar(self, n):
        """Hold FUNCTION and type a measure number."""
        self.hold('FUNCTION'); self.wait(50)
        for d in digits(n): self.press(DIGIT[d])
        self.release('FUNCTION'); self.wait(80)

def digits(n):
    """Panel digit sequence for a measure number, largest first: 131 -> [100, 3, 1], 55 -> [5, 5], 7 -> [7]."""
    out = []
    if n >= 100:
        out.append(100 * (n // 100)); n %= 100
        out += [n // 10, n % 10]
    elif n >= 10:
        out += [n // 10, n % 10]
    else:
        out.append(n)
    return out

def patbase(group, section, pattern): return 0x400 + group * 0x200 + section * 0x100 + pattern * 0x10

def write_pattern(ram, group, section, pattern, steps, triplet=False):
    """steps: list of (key, flags, time) with time 'n','t','r'; flags str of D U A S. Same format as Machine::writePattern."""
    ram = bytearray(ram); base = patbase(group, section, pattern)
    mx = 15 if triplet else 16; n = min(len(steps), mx)
    for i in range(16): ram[base + i] = 0xf; ram[base + 0x80 + i] = 0
    ns = -1
    for i in range(n):
        key, fl, tm = steps[i]
        tie = tm == 't' and ns >= 0
        if tm == 'r': ns = -1; continue
        src = steps[ns] if tie else steps[i]
        if not tie: ns = i
        f = ('D' in src[1]) * 1 | ('U' in src[1]) * 2 | ('A' in src[1]) * 4
        nxt = i + 1 < n and steps[i + 1][2] == 't'
        if nxt or 'S' in src[1]: f |= 8
        ram[base + i] = min(src[0], 12); ram[base + 0x80 + i] = f
        if tie: ram[base + 0x80 + i - 1] |= 8
    if n < mx: ram[base + n] = 0xe
    if triplet: ram[base + 15] = 0xd
    return bytes(ram)

def diff(a, b, lo=0, hi=0x400):
    return [(i, a[i], b[i]) for i in range(lo, hi) if a[i] != b[i]]

def fmt_events(evs, t0=0.0, types=(1, 2, 3)):
    s = []
    for t, ty, a, b in evs:
        if ty not in types: continue
        ms = (t - t0) * 1000
        if ty == 1: s.append(f"{ms:10.3f} ms  PI1={a} PI2={b}")
        elif ty == 2: s.append(f"{ms:10.3f} ms  strobe dac={a} acc={b}")
        else: s.append(f"{ms:10.3f} ms  INT")
    return "\n".join(s)
