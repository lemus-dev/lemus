// Computer keys by place: a panel's note, step and mode keys go by where a key sits, as on a US
// keyboard, so a German (QWERTZ) or French (AZERTY) keyboard plays the same shape the key list
// shows. host/KeyPlace.cpp sets the lookup from the computer's keyboard layout; a machine's tools,
// which don't link the host, leave it null and keep the typed character. PanelFrame.h's usKey()
// is what a panel calls.
#pragma once

namespace lemus::panel {

// The character the key that types c (lower case) types on a US keyboard, or c when the layout
// has no such key or the platform can't tell.
inline char32_t (*usKeyAt)(char32_t c) = nullptr;

// For the checks (tests/instrument.cpp): as usKeyAt, in a layout named for the platform (macOS
// "com.apple.keylayout.German", Windows "00000407"), whether or not it's the one in use. 0 when
// the computer doesn't have that layout or the platform has no lookup.
char32_t usKeyIn(const char *layout, char32_t c);

} // namespace lemus::panel
