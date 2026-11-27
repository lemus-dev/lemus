// The keyboard-layout lookup behind KeyPlace.h: which key types a character, and what that key
// types on a US keyboard. macOS asks the current layout (UCKeyTranslate) what each key of the
// main block types, unshifted and shifted; Windows asks it which key types the character
// (VkKeyScanEx) and where that key sits (its scan code). Linux asks xkbcommon what each key types
// in the X server's current layout, as macOS does; xkbcommon, its X11 part and xcb are loaded when
// first needed, so a computer without them still loads the plugin and keeps the typed character.
// No JUCE here: Carbon's and Windows' names clash with it.
#include "KeyPlace.h"

#if defined(__APPLE__)
#include <Carbon/Carbon.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#elif defined(__linux__)
#include <xcb/xcb.h>
#include <xkbcommon/xkbcommon-x11.h>
#include <xkbcommon/xkbcommon.h>

#include <dlfcn.h>
#include <mutex>
#endif

namespace lemus::panel {
namespace {

// A key of the main block that types a character, by place (macOS virtual key code, or the
// set-1 scan code that Windows uses and Linux's evdev codes match), and what it types on a US
// keyboard, unshifted.
struct Place
{
	unsigned code;
	char32_t us;
};

#if defined(__APPLE__)

const Place kPlaces[] = {
	{kVK_ANSI_Grave, '`'},       {kVK_ANSI_1, '1'},         {kVK_ANSI_2, '2'},          {kVK_ANSI_3, '3'},
	{kVK_ANSI_4, '4'},           {kVK_ANSI_5, '5'},         {kVK_ANSI_6, '6'},          {kVK_ANSI_7, '7'},
	{kVK_ANSI_8, '8'},           {kVK_ANSI_9, '9'},         {kVK_ANSI_0, '0'},          {kVK_ANSI_Minus, '-'},
	{kVK_ANSI_Equal, '='},       {kVK_ANSI_Q, 'q'},         {kVK_ANSI_W, 'w'},          {kVK_ANSI_E, 'e'},
	{kVK_ANSI_R, 'r'},           {kVK_ANSI_T, 't'},         {kVK_ANSI_Y, 'y'},          {kVK_ANSI_U, 'u'},
	{kVK_ANSI_I, 'i'},           {kVK_ANSI_O, 'o'},         {kVK_ANSI_P, 'p'},          {kVK_ANSI_LeftBracket, '['},
	{kVK_ANSI_RightBracket, ']'}, {kVK_ANSI_Backslash, '\\'}, {kVK_ANSI_A, 'a'},        {kVK_ANSI_S, 's'},
	{kVK_ANSI_D, 'd'},           {kVK_ANSI_F, 'f'},         {kVK_ANSI_G, 'g'},          {kVK_ANSI_H, 'h'},
	{kVK_ANSI_J, 'j'},           {kVK_ANSI_K, 'k'},         {kVK_ANSI_L, 'l'},          {kVK_ANSI_Semicolon, ';'},
	{kVK_ANSI_Quote, '\''},      {kVK_ANSI_Z, 'z'},         {kVK_ANSI_X, 'x'},          {kVK_ANSI_C, 'c'},
	{kVK_ANSI_V, 'v'},           {kVK_ANSI_B, 'b'},         {kVK_ANSI_N, 'n'},          {kVK_ANSI_M, 'm'},
	{kVK_ANSI_Comma, ','},       {kVK_ANSI_Period, '.'},    {kVK_ANSI_Slash, '/'},
};

// In the layout given: a key that types c unshifted wins over one that types it shifted.
char32_t lookUp(const UCKeyboardLayout *layout, char32_t c)
{
	char32_t shifted = 0;
	const UInt32 kinds[] = {0, (shiftKey >> 8) & 0xff};
	for (const UInt32 mods : kinds)
		for (const auto &p : kPlaces) {
			UInt32 dead = 0;
			UniChar s[4];
			UniCharCount n = 0;
			if (UCKeyTranslate(layout, UInt16(p.code), kUCKeyActionDown, mods, LMGetKbdType(), kUCKeyTranslateNoDeadKeysMask,
			                   &dead, 4, &n, s) != noErr
			    || n != 1 || char32_t(s[0]) != c)
				continue;
			if (mods == 0)
				return p.us;
			if (shifted == 0)
				shifted = p.us;
		}
	return shifted != 0 ? shifted : c;
}

// In an input source's layout; fallback when it has none.
char32_t lookUp(TISInputSourceRef source, char32_t c, char32_t fallback)
{
	const auto data = static_cast<CFDataRef>(TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData));
	return data != nullptr ? lookUp(reinterpret_cast<const UCKeyboardLayout *>(CFDataGetBytePtr(data)), c) : fallback;
}

char32_t fromLayout(char32_t c)
{
	TISInputSourceRef source = TISCopyCurrentKeyboardLayoutInputSource();
	if (source == nullptr)
		return c;
	const char32_t us = lookUp(source, c, c);
	CFRelease(source);
	return us;
}

char32_t inNamedLayout(const char *layout, char32_t c)
{
	CFStringRef id = CFStringCreateWithCString(nullptr, layout, kCFStringEncodingUTF8);
	const void *keys[] = {kTISPropertyInputSourceID}, *values[] = {id};
	CFDictionaryRef filter = CFDictionaryCreate(nullptr, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFArrayRef list = TISCreateInputSourceList(filter, true);
	char32_t us = 0;
	if (list != nullptr && CFArrayGetCount(list) > 0)
		us = lookUp(static_cast<TISInputSourceRef>(const_cast<void *>(CFArrayGetValueAtIndex(list, 0))), c, 0);
	if (list != nullptr)
		CFRelease(list);
	CFRelease(filter);
	CFRelease(id);
	return us;
}

#elif defined(_WIN32) || defined(__linux__)

const Place kPlaces[] = {
	{0x29, '`'}, {0x02, '1'}, {0x03, '2'}, {0x04, '3'}, {0x05, '4'},  {0x06, '5'}, {0x07, '6'}, {0x08, '7'},
	{0x09, '8'}, {0x0a, '9'}, {0x0b, '0'}, {0x0c, '-'}, {0x0d, '='},  {0x10, 'q'}, {0x11, 'w'}, {0x12, 'e'},
	{0x13, 'r'}, {0x14, 't'}, {0x15, 'y'}, {0x16, 'u'}, {0x17, 'i'},  {0x18, 'o'}, {0x19, 'p'}, {0x1a, '['},
	{0x1b, ']'}, {0x2b, '\\'}, {0x1e, 'a'}, {0x1f, 's'}, {0x20, 'd'}, {0x21, 'f'}, {0x22, 'g'}, {0x23, 'h'},
	{0x24, 'j'}, {0x25, 'k'}, {0x26, 'l'}, {0x27, ';'}, {0x28, '\''}, {0x2c, 'z'}, {0x2d, 'x'}, {0x2e, 'c'},
	{0x2f, 'v'}, {0x30, 'b'}, {0x31, 'n'}, {0x32, 'm'}, {0x33, ','},  {0x34, '.'}, {0x35, '/'},
};

#endif

#if defined(_WIN32)

char32_t lookUp(HKL layout, char32_t c)
{
	if (c == 0 || c > 0xffff)
		return c;
	const SHORT vk = VkKeyScanExW(WCHAR(c), layout);
	if (vk == -1)
		return c;
	const UINT scan = MapVirtualKeyExW(UINT(vk & 0xff), MAPVK_VK_TO_VSC, layout);
	for (const auto &p : kPlaces)
		if (p.code == scan)
			return p.us;
	return c;
}

char32_t fromLayout(char32_t c)
{
	return lookUp(GetKeyboardLayout(0), c);
}

// Loaded for the lookup, and unloaded again when the computer didn't have it loaded already.
char32_t inNamedLayout(const char *layout, char32_t c)
{
	wchar_t name[KL_NAMELENGTH] = {};
	for (int i = 0; i < KL_NAMELENGTH - 1 && layout[i] != 0; ++i)
		name[i] = wchar_t(layout[i]);
	HKL had[64];
	const int n = GetKeyboardLayoutList(64, had);
	const HKL hkl = LoadKeyboardLayoutW(name, KLF_NOTELLSHELL);
	if (hkl == nullptr)
		return 0;
	const char32_t us = lookUp(hkl, c);
	if (std::find(had, had + n, hkl) == had + n)
		UnloadKeyboardLayout(hkl);
	return us;
}

#elif defined(__linux__)

// xkbcommon's and xcb's functions, loaded once; null when a library or a function is missing.
struct Xkb
{
	decltype(&xkb_context_new) contextNew = nullptr;
	decltype(&xkb_keymap_new_from_names) keymapFromNames = nullptr;
	decltype(&xkb_keymap_unref) keymapUnref = nullptr;
	decltype(&xkb_keymap_key_get_syms_by_level) symsByLevel = nullptr;
	decltype(&xkb_state_unref) stateUnref = nullptr;
	decltype(&xkb_state_key_get_layout) keyLayout = nullptr;
	decltype(&xkb_keysym_to_utf32) toUtf32 = nullptr;
	decltype(&xkb_x11_setup_xkb_extension) setupX11 = nullptr;
	decltype(&xkb_x11_get_core_keyboard_device_id) coreKeyboard = nullptr;
	decltype(&xkb_x11_keymap_new_from_device) keymapFromDevice = nullptr;
	decltype(&xkb_x11_state_new_from_device) stateFromDevice = nullptr;
	decltype(&xcb_connect) connect = nullptr;
	decltype(&xcb_connection_has_error) hasError = nullptr;
	xkb_context *context = nullptr;
	xcb_connection_t *x = nullptr; // the X server's, kept open; null without one
	int32_t keyboard = -1;
	std::mutex lock;

	template <typename F> static void get(void *lib, F &f, const char *name)
	{
		f = lib != nullptr ? reinterpret_cast<F>(dlsym(lib, name)) : nullptr;
	}

	Xkb()
	{
		void *xkb = dlopen("libxkbcommon.so.0", RTLD_NOW | RTLD_LOCAL);
		get(xkb, contextNew, "xkb_context_new");
		get(xkb, keymapFromNames, "xkb_keymap_new_from_names");
		get(xkb, keymapUnref, "xkb_keymap_unref");
		get(xkb, symsByLevel, "xkb_keymap_key_get_syms_by_level");
		get(xkb, stateUnref, "xkb_state_unref");
		get(xkb, keyLayout, "xkb_state_key_get_layout");
		get(xkb, toUtf32, "xkb_keysym_to_utf32");
		if (contextNew == nullptr || keymapFromNames == nullptr || keymapUnref == nullptr || symsByLevel == nullptr || stateUnref == nullptr
		    || keyLayout == nullptr || toUtf32 == nullptr || (context = contextNew(XKB_CONTEXT_NO_FLAGS)) == nullptr)
			return;
		void *xkbX11 = dlopen("libxkbcommon-x11.so.0", RTLD_NOW | RTLD_LOCAL);
		void *xcb = dlopen("libxcb.so.1", RTLD_NOW | RTLD_LOCAL);
		get(xkbX11, setupX11, "xkb_x11_setup_xkb_extension");
		get(xkbX11, coreKeyboard, "xkb_x11_get_core_keyboard_device_id");
		get(xkbX11, keymapFromDevice, "xkb_x11_keymap_new_from_device");
		get(xkbX11, stateFromDevice, "xkb_x11_state_new_from_device");
		get(xcb, connect, "xcb_connect");
		get(xcb, hasError, "xcb_connection_has_error");
		if (setupX11 == nullptr || coreKeyboard == nullptr || keymapFromDevice == nullptr || stateFromDevice == nullptr || connect == nullptr
		    || hasError == nullptr)
			return;
		xcb_connection_t *c = connect(nullptr, nullptr);
		if (c == nullptr || hasError(c) != 0
		    || !setupX11(c, XKB_X11_MIN_MAJOR_XKB_VERSION, XKB_X11_MIN_MINOR_XKB_VERSION, XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS, nullptr,
		                 nullptr, nullptr, nullptr)
		    || (keyboard = coreKeyboard(c)) < 0)
			return; // the connection is left open: closing it would need xcb_disconnect, for nothing
		x = c;
	}
};

Xkb &xkb()
{
	static Xkb k;
	return k;
}

// In a keymap, each key in its current layout (state), or in the keymap's first one (no state):
// a key that types c unshifted (level 1) wins over one that types it shifted (level 2).
char32_t lookUp(Xkb &k, xkb_keymap *keymap, xkb_state *state, char32_t c, char32_t fallback)
{
	char32_t shifted = 0;
	for (const xkb_level_index_t level : {0u, 1u})
		for (const auto &p : kPlaces) {
			const xkb_keycode_t key = p.code + 8; // XKB's codes are evdev's plus 8
			const xkb_layout_index_t layout = state != nullptr ? k.keyLayout(state, key) : 0;
			const xkb_keysym_t *syms = nullptr;
			if (layout == XKB_LAYOUT_INVALID || k.symsByLevel(keymap, key, layout, level, &syms) != 1 || k.toUtf32(syms[0]) != c)
				continue;
			if (level == 0)
				return p.us;
			if (shifted == 0)
				shifted = p.us;
		}
	return shifted != 0 ? shifted : fallback;
}

char32_t fromLayout(char32_t c)
{
	Xkb &k = xkb();
	if (k.x == nullptr)
		return c;
	const std::lock_guard<std::mutex> hold(k.lock);
	xkb_keymap *keymap = k.keymapFromDevice(k.context, k.x, k.keyboard, XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (keymap == nullptr)
		return c;
	xkb_state *state = k.stateFromDevice(keymap, k.x, k.keyboard);
	const char32_t us = state != nullptr ? lookUp(k, keymap, state, c, c) : c;
	if (state != nullptr)
		k.stateUnref(state);
	k.keymapUnref(keymap);
	return us;
}

// A layout by its XKB name ("de"), whether or not the X server uses it.
char32_t inNamedLayout(const char *layout, char32_t c)
{
	Xkb &k = xkb();
	if (k.context == nullptr)
		return 0;
	const std::lock_guard<std::mutex> hold(k.lock);
	const xkb_rule_names names = {"evdev", "pc105", layout, "", ""};
	xkb_keymap *keymap = k.keymapFromNames(k.context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (keymap == nullptr)
		return 0;
	const char32_t us = lookUp(k, keymap, nullptr, c, c);
	k.keymapUnref(keymap);
	return us;
}

#endif

} // namespace

void useKeyboardLayout()
{
#if defined(__APPLE__) || defined(_WIN32) || defined(__linux__)
	usKeyAt = &fromLayout;
#endif
}

char32_t usKeyIn(const char *layout, char32_t c)
{
#if defined(__APPLE__) || defined(_WIN32) || defined(__linux__)
	return inNamedLayout(layout, c);
#else
	(void)layout;
	(void)c;
	return 0;
#endif
}

} // namespace lemus::panel
