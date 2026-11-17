#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "win.h"

#include <cstdio>

namespace win {
namespace {
HKL g_saved = nullptr;
}

bool selectGerman()
{
	if (g_saved == nullptr)
		g_saved = GetKeyboardLayout(0);
	const HKL hkl = LoadKeyboardLayoutW(L"00000407", KLF_ACTIVATE);
	if (hkl == nullptr)
		return false;
	ActivateKeyboardLayout(hkl, 0);
	return GetKeyboardLayout(0) == hkl;
}

void restoreLayout()
{
	if (g_saved != nullptr)
		ActivateKeyboardLayout(g_saved, 0);
}

std::string layoutName()
{
	char name[KL_NAMELENGTH] = {};
	GetKeyboardLayoutNameA(name);
	char out[64];
	std::snprintf(out, sizeof out, "%s (HKL %p)", name, (void *)GetKeyboardLayout(0));
	return out;
}

unsigned short typedBy(unsigned short scan)
{
	const HKL l = GetKeyboardLayout(0);
	const UINT vk = MapVirtualKeyExW(scan, MAPVK_VSC_TO_VK, l);
	UINT ch = MapVirtualKeyExW(vk, MAPVK_VK_TO_CHAR, l) & 0x7fffffff;
	if (ch >= 'A' && ch <= 'Z')
		ch += 'a' - 'A';
	return (unsigned short)ch;
}

void toFront(void *hwnd)
{
	const HWND h = (HWND)hwnd;
	INPUT alt[2] = {};
	alt[0].type = INPUT_KEYBOARD;
	alt[0].ki.wVk = VK_MENU;
	alt[1] = alt[0];
	alt[1].ki.dwFlags = KEYEVENTF_KEYUP;
	SendInput(2, alt, sizeof(INPUT)); // lets this process take the foreground
	ShowWindow(h, SW_SHOW);
	SetForegroundWindow(h);
	BringWindowToTop(h);
	SetActiveWindow(h);
}

bool isForeground(void *hwnd)
{
	return GetForegroundWindow() == (HWND)hwnd;
}

std::string focusClass()
{
	const HWND f = GetFocus();
	if (f == nullptr)
		return "(none)";
	char cls[128] = {};
	GetClassNameA(f, cls, sizeof cls);
	return cls;
}

void mouse(int x, int y, bool down)
{
	SetCursorPos(x, y);
	INPUT in = {};
	in.type = INPUT_MOUSE;
	in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
	SendInput(1, &in, sizeof(INPUT));
}

void key(unsigned short scan, bool down)
{
	INPUT in = {};
	in.type = INPUT_KEYBOARD;
	in.ki.wScan = scan;
	in.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
	SendInput(1, &in, sizeof(INPUT));
}

bool capture(void *hwnd, bool fromScreen, int &w, int &h, std::vector<unsigned char> &bgra)
{
	const HWND hw = (HWND)hwnd;
	RECT r;
	if (!GetWindowRect(hw, &r))
		return false;
	w = r.right - r.left;
	h = r.bottom - r.top;
	const HDC screen = GetDC(nullptr);
	const HDC mem = CreateCompatibleDC(screen);
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void *bits = nullptr;
	const HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	const HGDIOBJ old = SelectObject(mem, bmp);
	BOOL ok = FALSE;
	if (!fromScreen)
		ok = PrintWindow(hw, mem, 2 /* PW_RENDERFULLCONTENT */);
	if (!ok)
		ok = BitBlt(mem, 0, 0, w, h, screen, r.left, r.top, SRCCOPY);
	GdiFlush();
	if (ok && bits != nullptr)
		bgra.assign((unsigned char *)bits, (unsigned char *)bits + size_t(w) * size_t(h) * 4);
	SelectObject(mem, old);
	DeleteObject(bmp);
	DeleteDC(mem);
	ReleaseDC(nullptr, screen);
	return ok && bits != nullptr;
}
} // namespace win
