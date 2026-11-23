#include "warm.h"

namespace xpander {

namespace {
constexpr uint16_t kFont = 0xb955; // main ROM 1.2: 16-bit anode patterns, little-endian, by character
constexpr double kBootSeconds = 3.0;
} // namespace

std::string displayText(const uint8_t* mainRom, const MainBoard::Segments& segments, int d)
{
    std::string t;
    for (int g = 0; g < MainBoard::kDigits; ++g)
    {
        const uint16_t w = segments[size_t(d)][size_t(g)] & 0xfffe; // bit 0: the underline
        char c = '#';
        for (int ch = 0x20; ch < 0x60; ++ch)
        {
            const size_t i = size_t(kFont - 0x8000 + 2 * ch);
            if ((mainRom[i] | mainRom[i + 1] << 8) == w)
            {
                c = char(ch);
                break;
            }
        }
        t += c;
    }
    return t;
}

void boot(Machine& m)
{
    m.powerOn();
    m.idle(int(kBootSeconds * 48000));
}

WarmUp warmUp(Machine& m, const uint8_t* mainRom, const std::function<bool()>& cancel, const std::vector<uint8_t>& patches)
{
    WarmUp r;
    const int rate = 48000;
    auto& main = m.main();
    auto text = [&](int d) { return displayText(mainRom, main.display(), d); };
    auto idle = [&](double s) {
        m.idle(int(s * rate));
        r.seconds += s;
    };
    auto tap = [&](int col, int row) {
        main.setSwitch(col, row, true);
        idle(0.05);
        main.setSwitch(col, row, false);
        idle(0.05);
    };
    m.powerOn();
    // The firmware finds its memory blank and asks; YES is the soft button under it (P2 1).
    for (int i = 0; i < 40 && text(1).find("RESET MEM") == std::string::npos; ++i)
        idle(0.1);
    if (text(1).find("RESET MEM") == std::string::npos)
    {
        r.message = "no RESET MEMORY prompt";
        return r;
    }
    idle(0.3);
    tap(4, 0);
    // It resets and starts again on the VCO 1 page of the single patch being edited.
    auto blank = [](const std::string& t) { return t.find_first_not_of(' ') == std::string::npos; };
    for (int i = 0; i < 100 && (blank(text(1)) || text(1).find("RESET MEM") != std::string::npos); ++i)
        idle(0.1);
    idle(0.5);
    // TUNE, then ALL (P1 6): every voice is measured through OSC, so the voices run now.
    tap(1, 6);
    tap(3, 5);
    std::vector<float> l(4800), rr(4800);
    for (int i = 0; i < 4000; ++i)
    {
        if (cancel && cancel())
        {
            r.message = "cancelled";
            return r;
        }
        m.process(l.data(), rr.data(), 4800);
        r.seconds += 0.1;
        // ALL reports five verdicts in a row (VCOS PW RES VCF VCA) once the last is done.
        const auto t3 = text(2);
        int verdicts = 0, passes = 0;
        for (size_t at = 0; (at = t3.find_first_of("PF", at)) != std::string::npos; ++at)
        {
            if (t3.compare(at, 4, "PASS") == 0)
                ++verdicts, ++passes;
            else if (t3.compare(at, 4, "FAIL") == 0)
                ++verdicts;
        }
        if (verdicts >= 5 && t3.find("TUNE") == std::string::npos)
        {
            r.message = t3;
            r.ok = passes == verdicts;
            break;
        }
    }
    if (!patches.empty())
    {
        // MASTER PAGE, MIDI (P1 2), ENABLES (P1 3), SYSTEMX (P1 1) on: the firmware takes patch
        // dumps only with it. The dumps arrive at 31.25 kbaud (about 0.13 s a patch); each is stored
        // into its program once all its bytes are in. Then SYSTEMX off again, its default.
        tap(1, 7);
        tap(3, 1);
        tap(3, 2);
        tap(3, 0);
        idle(0.2);
        for (const uint8_t b : patches)
            main.midiIn(b);
        while (main.midiInPending() > 0)
        {
            if (cancel && cancel())
            {
                r.message = "cancelled";
                return r;
            }
            idle(0.5);
        }
        idle(0.5);
        tap(3, 0);
        // Patch 00 into the edit buffer, which still holds the default OBERHEIM patch: + selects
        // patch 01, - goes back to 00. (SINGLE would compare the edited buffer with the stored one.)
        tap(1, 2);
        tap(1, 3);
        idle(0.3);
        r.patch = text(0).substr(0, 12);
        if (r.patch.compare(0, 3, "S00") != 0 || blank(r.patch.substr(4)))
        {
            r.ok = false;
            r.message = "patches not received: " + r.patch;
        }
    }
    // Back to the VCO 1 page (VCO1), where the unit starts next time: the page is kept in battery
    // RAM. (SINGLE here would start entering a patch number instead.)
    tap(3, 6);
    idle(0.5);
    return r;
}

} // namespace xpander
