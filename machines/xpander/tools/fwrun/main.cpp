// Runs the Xpander's two CPUs headless from power-on (no analog voices: OSC stays low) and prints
// what the panel would show: the VFD segments, the LEDs, and the voice board's activity.
//   xpanderfw ROMDIR [seconds] [script...]
// ROMDIR holds exp1.2-0.u8 .. exp1.2-3.u5 and ca1.4-0.u912, ca1.4-1.u914 (MAME names).
// Script steps: "press:C,R" / "release:C,R" (switch column, row), "enc:N,STEPS", "wait:SECONDS",
// "midi:HEX..." (bytes into MIDI IN), "show".
#include "xpander/core/mainboard.h"
#include "xpander/core/voiceboard.h"

#include <cstdio>
#include <cstdlib>
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

struct Rig
{
    std::vector<uint8_t> mainRom, voiceRom;
    VoiceBoard voice;
    MainBoard main;
    uint64_t voiceEvents = 0, selects = 0, latches = 0, end = 0;
    Rig(std::vector<uint8_t> m, std::vector<uint8_t> v)
        : mainRom(std::move(m)), voiceRom(std::move(v)), voice(voiceRom.data()), main(mainRom.data(), voice)
    {
        main.powerOn();
    }
    void run(double seconds)
    {
        const uint64_t cycles = uint64_t(seconds * 2e6);
        for (uint64_t done = 0; done < cycles; done += 10)
        {
            end += 10;
            main.run(end);
            voice.run(end, nullptr, 0);
            auto& ev = voice.events();
            for (const auto& e : ev)
            {
                ++voiceEvents;
                selects += e.kind == VoiceEvent::Select;
                latches += e.kind == VoiceEvent::Latch;
            }
            ev.clear();
        }
    }
    // The text a display shows, through the font table in main ROM 1.2 (little-endian words at
    // $B955 + 2 x character): unknown patterns print as '#'.
    std::string text(int d) const
    {
        std::string t;
        for (int g = 0; g < MainBoard::kDigits; ++g)
        {
            const uint16_t w = main.display()[size_t(d)][size_t(g)];
            char c = '#';
            for (int ch = 0x20; ch < 0x60; ++ch)
            {
                const size_t i = size_t(0xb955 - 0x8000 + 2 * ch);
                if ((mainRom[i] | mainRom[i + 1] << 8) == (w & 0xfffe))
                {
                    c = char(ch);
                    break;
                }
            }
            t += c;
        }
        return t;
    }
    void show()
    {
        // The FG405A2's 16 anodes, as MAME's layout maps them to a 14-segment digit: bit 0 is the
        // underline; the rest by bitswap<15>(1,3,6,5,4,2,7,12,11,10,13,15,14,9,8).
        for (int d = 0; d < MainBoard::kDisplays; ++d)
            std::printf("VFD%d |%s|\n", d + 1, text(d).c_str());
        std::printf("LED1 %02x LED2 %02x HALTSET %02x  main PC %04x  voice PC %04x  voice events %llu (S&H %llu, latch %llu)\n",
                    main.led1(), main.led2(), main.haltSet(), main.cpu().pc(), voice.cpu().pc(),
                    (unsigned long long)voiceEvents, (unsigned long long)selects, (unsigned long long)latches);
    }
};

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: xpanderfw ROMDIR [seconds] [script...]\n");
        return 2;
    }
    const std::string dir = argv[1];
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
    Rig rig(mainRom, voiceRom);
    rig.run(argc > 2 ? std::atof(argv[2]) : 3.0);
    rig.show();
    for (int i = 3; i < argc; ++i)
    {
        std::string s = argv[i];
        int a = 0, b = 0;
        if (std::sscanf(s.c_str(), "press:%d,%d", &a, &b) == 2)
            rig.main.setSwitch(a, b, true);
        else if (std::sscanf(s.c_str(), "release:%d,%d", &a, &b) == 2)
            rig.main.setSwitch(a, b, false);
        else if (std::sscanf(s.c_str(), "enc:%d,%d", &a, &b) == 2)
            rig.main.turnEncoder(a, b);
        else if (s.rfind("wait:", 0) == 0)
            rig.run(std::atof(s.c_str() + 5));
        else if (s.rfind("midi:", 0) == 0)
            for (size_t k = 5; k + 1 < s.size(); k += 2)
                rig.main.midiIn(uint8_t(std::stoi(s.substr(k, 2), nullptr, 16)));
        else if (s == "show")
            rig.show();
    }
    return 0;
}
