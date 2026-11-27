// Tokyo Night, as Onyx and the Lemus host use it, and JetBrains Mono (bundled).
import AppKit

extension NSColor {
	convenience init(hex: UInt32, alpha: CGFloat = 1) {
		self.init(srgbRed: CGFloat((hex >> 16) & 0xFF) / 255, green: CGFloat((hex >> 8) & 0xFF) / 255,
		          blue: CGFloat(hex & 0xFF) / 255, alpha: alpha)
	}
}

enum Theme {
	static let ground = NSColor(hex: 0x1A1B26)
	static let panel = NSColor(hex: 0x16161E)
	static let well = NSColor(hex: 0x292E42)
	static let selection = NSColor(hex: 0x33467C)
	static let ink = NSColor(hex: 0xC0CAF5)
	static let inkSoft = NSColor(hex: 0xA9B1D6)
	static let muted = NSColor(hex: 0x737AA2)
	static let rule = NSColor(hex: 0x565F89, alpha: 0.45)
	static let accent = NSColor(hex: 0x7AA2F7)
	static let title = NSColor(hex: 0xA9C1FF)
	static let onAccent = NSColor(hex: 0x16161E)
	static let warn = NSColor(hex: 0xE0AF68)
	static let ok = NSColor(hex: 0x9ECE6A)
	static let red = NSColor(hex: 0xF7768E)

	static func mono(_ size: CGFloat, bold: Bool = false) -> NSFont {
		NSFont(name: bold ? "JetBrainsMono-Bold" : "JetBrainsMono-Regular", size: size)
			?? .monospacedSystemFont(ofSize: size, weight: bold ? .bold : .regular)
	}

	static func registerFonts() {
		for url in Bundle.main.urls(forResourcesWithExtension: "ttf", subdirectory: nil) ?? [] {
			CTFontManagerRegisterFontsForURL(url as CFURL, .process, nil)
		}
	}
}

extension NSAttributedString {
	static func styled(_ s: String, _ font: NSFont, _ colour: NSColor, kern: CGFloat = 0) -> NSAttributedString {
		NSAttributedString(string: s, attributes: [.font: font, .foregroundColor: colour, .kern: kern])
	}
}
