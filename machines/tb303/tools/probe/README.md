# probe: black-box harness for clean-room questions

Runs the original TB-303 ROM in the board model (`core/machine.cpp`) and observes it only from
outside: panel keys and switches, RUN and the tempo clock, the PI1/PI2 pins and latched DAC
code/accent at one-machine-cycle (8.8 µs) resolution, INT pulse times, LED drive, and the
external RAM. It exposes nothing of the CPU's internals, so what it shows can go into
`~/src/cleanroom/<device>/reports/` (see that repo's PROVENANCE.md for the rules).

Build (from `lemus/`):

    c++ -std=c++17 -O2 -shared -fPIC -Imachines -o machines/tb303/tools/probe/libprobe.dylib \
        machines/tb303/tools/probe/probe.cpp machines/tb303/core/machine.cpp machines/tb303/core/ucom43.cpp

Use from Python (`tb.py`; the ROM is read from `../private/roms/tb303/`):

    from tb import *
    ram = write_pattern(bytes(3072), 0, 0, 0, [(0, '', 'n'), (2, 'S', 'n'), (4, '', 'r')])
    tb = TB(ram, 'pp', 1)              # MODE, TRACK/GROUP switch
    tb.wait(300); tb.run(True, bpm=120)
    tb.wait(600); print(fmt_events(tb.events()))
    tb.press('TAP'); tb.bar(131); tb.ram()

The scripts that answered cleanroom questions 001–012 (2026-10-09) are kept in
`../private/tb303/probe-runs/`.
