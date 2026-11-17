// The Win32 side of keyhost_win.cpp (windows.h kept out of the JUCE file).
#pragma once
#include <string>
#include <vector>

namespace win {
bool selectGerman();           // German (00000407) for this thread
void restoreLayout();          // the layout this thread had first
std::string layoutName();      // this thread's layout
unsigned short typedBy(unsigned short scan); // what the key at this scan code types, lower case
void toFront(void *hwnd);
bool isForeground(void *hwnd);
std::string focusClass();      // the window class with keyboard focus
void mouse(int x, int y, bool down);
void key(unsigned short scan, bool down);
// The window's pixels, top-down BGRA: PrintWindow (full content), or from the screen.
bool capture(void *hwnd, bool fromScreen, int &w, int &h, std::vector<unsigned char> &bgra);
} // namespace win
