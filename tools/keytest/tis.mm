// Keyboard layout side of keyhost (Carbon kept away from JUCE).
#import <Carbon/Carbon.h>
#import <Foundation/Foundation.h>

static TISInputSourceRef g_saved = nullptr;
static TISInputSourceRef g_german = nullptr;
static bool g_enabledGerman = false;

extern "C" void saveLayout()
{
	if (g_saved == nullptr)
		g_saved = TISCopyCurrentKeyboardInputSource();
}

extern "C" bool selectGerman()
{
	saveLayout();
	if (g_german == nullptr) {
		NSDictionary *filter = @{(__bridge NSString *)kTISPropertyInputSourceID: @"com.apple.keylayout.German"};
		CFArrayRef list = TISCreateInputSourceList((__bridge CFDictionaryRef)filter, true);
		if (list == nullptr || CFArrayGetCount(list) == 0)
			return false;
		g_german = (TISInputSourceRef)CFRetain(CFArrayGetValueAtIndex(list, 0));
		CFRelease(list);
	}
	if (![(__bridge NSNumber *)TISGetInputSourceProperty(g_german, kTISPropertyInputSourceIsEnabled) boolValue]) {
		TISEnableInputSource(g_german);
		g_enabledGerman = true;
	}
	return TISSelectInputSource(g_german) == noErr;
}

// Back to the layout in use before; with final, also disables German if we enabled it.
extern "C" void restoreLayout(bool final)
{
	if (g_saved != nullptr)
		TISSelectInputSource(g_saved);
	if (final && g_enabledGerman && g_german != nullptr) {
		TISDisableInputSource(g_german);
		g_enabledGerman = false;
	}
}

extern "C" void currentLayoutId(char *out, int n)
{
	TISInputSourceRef s = TISCopyCurrentKeyboardLayoutInputSource();
	NSString *sid = (__bridge NSString *)TISGetInputSourceProperty(s, kTISPropertyInputSourceID);
	strlcpy(out, sid.UTF8String, size_t(n));
	CFRelease(s);
}

// What the key with this virtual key code types unshifted in the current layout.
extern "C" unsigned short typedBy(unsigned short code)
{
	TISInputSourceRef s = TISCopyCurrentKeyboardLayoutInputSource();
	CFDataRef data = (CFDataRef)TISGetInputSourceProperty(s, kTISPropertyUnicodeKeyLayoutData);
	UInt32 dead = 0;
	UniChar c[4] = {};
	UniCharCount n = 0;
	UCKeyTranslate(reinterpret_cast<const UCKeyboardLayout *>(CFDataGetBytePtr(data)), code, kUCKeyActionDown, 0, LMGetKbdType(),
	               kUCKeyTranslateNoDeadKeysMask, &dead, 4, &n, c);
	CFRelease(s);
	return n == 1 ? c[0] : 0;
}
