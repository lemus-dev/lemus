// Renders the Xpander headless from power-on, with a script of panel presses and MIDI.
//   xpanderrender ROMDIR OUT.wav [--rate R] [--ram FILE] STEP...
// --ram starts from a saved battery RAM (e.g. Lemus's factory-r3-patches.ram) instead of blank.
// Steps, in order (times are absolute seconds from power-on):
//   @T                     run to time T
//   press:C,R  release:C,R tap:C,R    switch column/row (tap = press, 50 ms, release)
//   enc:N,STEPS            encoder N, STEPS detents
//   on:NOTE[,VEL]  off:NOTE   MIDI note on channel 1;  midi:HEX  raw bytes
//   vol:X                  VOLUME 0-1
//   vcoasym:X              both VCOs of every voice: up/down current mismatch X (the model: 0.003)
//   cv:N,V                 input N (0-5 CV1-6, 6-7 PEDAL 1-2) at V volts at the jack; cv:N,off unplugs it
//   show                   print the three displays
//   patch:NN               single patch NN, typed on the keypad (0.6 s)
//   saveram:FILE           write the battery RAM as it is now (xpanderplugintest --state makes a plugin state of it)
// The WAV holds only the time after the first "rec" step (or everything without one).
#include "xpander/core/machine.h"

#include <bitset>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace xpander;

static std::vector<uint8_t> load(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
    {
        std::fprintf(stderr, "can't read %s\n", path.c_str());
        std::exit(1);
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

static void writeWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int rate)
{
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t frames = uint32_t(l.size());
    f.write("RIFF", 4);
    u32(36 + frames * 8);
    f.write("WAVEfmt ", 8);
    u32(16);
    u16(3);
    u16(2);
    u32(uint32_t(rate));
    u32(uint32_t(rate) * 8);
    u16(8);
    u16(32);
    f.write("data", 4);
    u32(frames * 8);
    for (uint32_t i = 0; i < frames; ++i)
    {
        f.write(reinterpret_cast<const char*>(&l[i]), 4);
        f.write(reinterpret_cast<const char*>(&r[i]), 4);
    }
}

static std::string text(const std::vector<uint8_t>& rom, const MainBoard::Segments& seg, int d)
{
    std::string t;
    for (int g = 0; g < MainBoard::kDigits; ++g)
    {
        const uint16_t w = seg[size_t(d)][size_t(g)] & 0xfffe;
        char c = '#';
        for (int ch = 0x20; ch < 0x60; ++ch)
        {
            const size_t i = size_t(0xb955 - 0x8000 + 2 * ch);
            if ((rom[i] | rom[i + 1] << 8) == w)
            {
                c = char(ch);
                break;
            }
        }
        t += c;
    }
    return t;
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: xpanderrender ROMDIR OUT.wav [--rate R] STEP...\n");
        return 2;
    }
    const std::string dir = argv[1], out = argv[2];
    int rate = 48000;
    std::vector<uint8_t> mainRom, voiceRom;
    for (const char* n : {"exp1.2-0.u8", "exp1.2-1.u7", "exp1.2-2.u6", "exp1.2-3.u5"})
    {
        auto r = load(dir + "/" + n);
        mainRom.insert(mainRom.end(), r.begin(), r.end());
    }
    for (const char* n : {"ca1.4-0.u912", "ca1.4-1.u914"})
    {
        auto r = load(dir + "/" + n);
        voiceRom.insert(voiceRom.end(), r.begin(), r.end());
    }
    int first = 3;
    std::vector<uint8_t> ram;
    while (first + 1 < argc && argv[first][0] == '-' && argv[first][1] == '-')
    {
        if (std::strcmp(argv[first], "--rate") == 0)
            rate = std::atoi(argv[first + 1]);
        else if (std::strcmp(argv[first], "--ram") == 0)
            ram = load(argv[first + 1]);
        first += 2;
    }
    Machine m(mainRom.data(), voiceRom.data());
    m.prepare(rate);
    if (ram.size() == m.main().ram().size())
        std::copy(ram.begin(), ram.end(), m.main().ram().begin());
    m.powerOn();

    std::vector<float> L, R;
    bool recording = true;
    size_t recStart = 0;
    double now = 0;
    std::vector<float> l(256), r(256);
    auto runTo = [&](double t) {
        while (now < t)
        {
            const int n = std::max(1, std::min(256, int((t - now) * rate)));
            m.process(l.data(), r.data(), n);
            if (recording)
            {
                L.insert(L.end(), l.begin(), l.begin() + n);
                R.insert(R.end(), r.begin(), r.begin() + n);
            }
            now += double(n) / rate;
        }
    };
    for (int i = first; i < argc; ++i)
    {
        const std::string s = argv[i];
        int a = 0, b = 0;
        if (s[0] == '@')
            runTo(std::atof(s.c_str() + 1));
        else if (s == "rec")
        {
            L.clear();
            R.clear();
            recStart = 0;
        }
        else if (std::sscanf(s.c_str(), "press:%d,%d", &a, &b) == 2)
            m.main().setSwitch(a, b, true);
        else if (std::sscanf(s.c_str(), "release:%d,%d", &a, &b) == 2)
            m.main().setSwitch(a, b, false);
        else if (std::sscanf(s.c_str(), "tap:%d,%d", &a, &b) == 2)
        {
            m.main().setSwitch(a, b, true);
            runTo(now + 0.05);
            m.main().setSwitch(a, b, false);
            runTo(now + 0.05);
        }
        else if (s.rfind("saveram:", 0) == 0)
        {
            std::ofstream f(s.substr(8), std::ios::binary);
            f.write(reinterpret_cast<const char*>(m.main().ram().data()), std::streamsize(m.main().ram().size()));
        }
        else if (std::sscanf(s.c_str(), "patch:%d", &a) == 1)
        {
            // The keypad's two digits: 0-7 are column 0, 8 and 9 column 1.
            for (const int d : {a / 10 % 10, a % 10})
            {
                const int col = d < 8 ? 0 : 1, row = d < 8 ? d : d - 8;
                m.main().setSwitch(col, row, true);
                runTo(now + 0.05);
                m.main().setSwitch(col, row, false);
                runTo(now + 0.25);
            }
        }
        else if (std::sscanf(s.c_str(), "enc:%d,%d", &a, &b) == 2)
        {
            for (int k = 0; k < std::abs(b); ++k)
            {
                m.main().turnEncoder(a, b > 0 ? 1 : -1);
                runTo(now + 0.01);
            }
        }
        else if (s.rfind("on:", 0) == 0)
        {
            int note = 60, vel = 100;
            std::sscanf(s.c_str(), "on:%d,%d", &note, &vel);
            m.main().midiIn(0x90);
            m.main().midiIn(uint8_t(note));
            m.main().midiIn(uint8_t(vel));
        }
        else if (std::sscanf(s.c_str(), "off:%d", &a) == 1)
        {
            m.main().midiIn(0x80);
            m.main().midiIn(uint8_t(a));
            m.main().midiIn(64);
        }
        else if (s.rfind("midi:", 0) == 0)
            for (size_t k = 5; k + 1 < s.size(); k += 2)
                m.main().midiIn(uint8_t(std::stoi(s.substr(k, 2), nullptr, 16)));
        else if (s.rfind("vcoasym:", 0) == 0)
        {
            const double a = std::atof(s.c_str() + 8);
            for (int v = 0; v < 6; ++v)
            {
                m.voice(v).vco1().params.asym = a;
                m.voice(v).vco2().params.asym = a;
            }
        }
        else if (s.rfind("cv:", 0) == 0)
        {
            int n = 0;
            char val[32] = {};
            if (std::sscanf(s.c_str(), "cv:%d,%31s", &n, val) == 2 && n >= 0 && n < 8)
                m.main().setCvInput(n, std::string(val) == "off" ? std::numeric_limits<double>::quiet_NaN() : std::atof(val));
        }
        else if (s.rfind("vol:", 0) == 0)
            m.setVolume(std::atof(s.c_str() + 4));
        else if (s == "consts")
        {
            const auto& d = m.voiceBoard().dac;
            std::printf("consts ref4v %.4f dacOffset %.6f refFixed %.4f\n", m.voice(0).vco1().params.vTh, d.offset, d.refFixed);
        }
        else if (s == "show")
        {
            for (int d = 0; d < 3; ++d)
                std::printf("%6.2f VFD%d |%s|\n", now, d + 1, text(mainRom, m.main().display(), d).c_str());
            float peak = 0;
            for (size_t k = recStart; k < L.size(); ++k)
                peak = std::max(peak, std::max(std::fabs(L[k]), std::fabs(R[k])));
            int lit = 0;
            for (const auto& disp : m.main().display())
                for (const auto w : disp)
                    lit += static_cast<int>(std::bitset<16>(w).count());
            std::printf("       peak so far %.4f  voice board rev flag $1E01=%02x  mute %d  LED1 %02x LED2 %02x  segments lit %d\n", peak,
                        m.voiceBoard().ram()[0x1e01], m.voiceBoard().mute() ? 1 : 0, m.main().led1(), m.main().led2(), lit);
            for (int v = 0; v < 6; ++v)
            {
                const auto& vc = m.voice(v);
                std::printf("       v%d cv:", v + 1);
                for (int c = 0; c < Voice::kChannels; ++c)
                    std::printf(" %6.3f", vc.cv(c));
                std::printf("  vca pin %.3f  vco1 %.1f Hz vco2 %.1f Hz  latches %02x %02x %02x\n", vc.vcaCv(), vc.vco1().hz(),
                            vc.vco2().hz(), vc.latches()[0], vc.latches()[1], vc.latches()[2]);
            }
        }
        else
            std::fprintf(stderr, "unknown step %s\n", s.c_str());
    }
    writeWav(out, L, R, rate);
    return 0;
}
