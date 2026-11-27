// The small Tokyo Night controls: pills (a button, or a menu), the play button, a number field,
// the level meter and the plugin list shown when no plugin is loaded.
import AppKit

class Pill: NSView {
	var title: String { didSet { invalidateIntrinsicContentSize(); needsDisplay = true } }
	var menu_: (() -> NSMenu)?
	var action: (() -> Void)?
	var highlighted = false { didSet { needsDisplay = true } }
	private var hover = false { didSet { needsDisplay = true } }

	init(_ title: String) {
		self.title = title
		super.init(frame: .zero)
		addTrackingArea(NSTrackingArea(rect: .zero, options: [.mouseEnteredAndExited, .activeInActiveApp, .inVisibleRect], owner: self))
	}
	required init?(coder: NSCoder) { fatalError() }

	var label: NSAttributedString {
		.styled(title + (menu_ != nil ? "  ▾" : ""), Theme.mono(13, bold: true), highlighted ? Theme.onAccent : Theme.ink)
	}
	override var intrinsicContentSize: NSSize { NSSize(width: ceil(label.size().width) + 30, height: 34) }
	override func mouseEntered(with event: NSEvent) { hover = true }
	override func mouseExited(with event: NSEvent) { hover = false }
	override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

	override func draw(_ dirty: NSRect) {
		let path = NSBezierPath(roundedRect: bounds.insetBy(dx: 0.5, dy: 0.5), xRadius: 8, yRadius: 8)
		(highlighted ? Theme.accent : hover ? Theme.selection : Theme.well).setFill()
		path.fill()
		let l = label
		let s = l.size()
		l.draw(at: NSPoint(x: (bounds.width - s.width) / 2, y: (bounds.height - s.height) / 2))
	}

	override func mouseDown(with event: NSEvent) {
		if let m = menu_?() {
			m.appearance = NSAppearance(named: .darkAqua)
			m.font = Theme.mono(12)
			m.popUp(positioning: nil, at: NSPoint(x: 0, y: bounds.height + 4), in: self)
		} else {
			action?()
		}
	}
	override var isFlipped: Bool { true }
}

final class PlayButton: NSView {
	var playing = false { didSet { needsDisplay = true } }
	var action: (() -> Void)?
	override var isFlipped: Bool { true }
	override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }
	override func mouseDown(with event: NSEvent) { action?() }
	override func draw(_ dirty: NSRect) {
		let path = NSBezierPath(roundedRect: bounds.insetBy(dx: 0.5, dy: 0.5), xRadius: 8, yRadius: 8)
		(playing ? Theme.accent : Theme.well).setFill()
		path.fill()
		let c = NSPoint(x: bounds.midX, y: bounds.midY)
		(playing ? Theme.onAccent : Theme.ink).setFill()
		if playing {
			NSBezierPath(roundedRect: NSRect(x: c.x - 6.5, y: c.y - 6.5, width: 13, height: 13), xRadius: 1.5, yRadius: 1.5).fill()
		} else {
			let t = NSBezierPath()
			t.move(to: NSPoint(x: c.x - 5, y: c.y - 8))
			t.line(to: NSPoint(x: c.x + 8, y: c.y))
			t.line(to: NSPoint(x: c.x - 5, y: c.y + 8))
			t.close()
			t.fill()
		}
	}
}

// A text cell that centres its line vertically (NSTextField puts it at the top).
final class CentredCell: NSTextFieldCell {
	private func centred(_ r: NSRect) -> NSRect {
		let h = cellSize(forBounds: r).height
		return NSRect(x: r.minX, y: r.minY + ((r.height - h) / 2).rounded(), width: r.width, height: h)
	}
	override func titleRect(forBounds r: NSRect) -> NSRect { centred(super.titleRect(forBounds: r)) }
	override func drawInterior(withFrame f: NSRect, in v: NSView) { super.drawInterior(withFrame: centred(f), in: v) }
	override func edit(withFrame f: NSRect, in v: NSView, editor: NSText, delegate: Any?, event: NSEvent?) {
		super.edit(withFrame: centred(f), in: v, editor: editor, delegate: delegate, event: event)
	}
	override func select(withFrame f: NSRect, in v: NSView, editor: NSText, delegate: Any?, start: Int, length: Int) {
		super.select(withFrame: centred(f), in: v, editor: editor, delegate: delegate, start: start, length: length)
	}
}

// A number you type (Return) or scroll on.
final class NumberField: NSTextField, NSTextFieldDelegate {
	override class var cellClass: AnyClass? { get { CentredCell.self } set {} }
	var range: ClosedRange<Double> = 20...300
	var step: Double = 1
	var commit: ((Double) -> Void)?
	var value: Double = 120 {
		didSet { if !isEditingNow { stringValue = Self.format(value) } }
	}
	private var isEditingNow: Bool { currentEditor() != nil }

	static func format(_ v: Double) -> String {
		v == v.rounded() ? String(Int(v)) : String(format: "%.2f", v)
	}

	init() {
		super.init(frame: .zero)
		isBordered = false
		drawsBackground = false
		textColor = Theme.ink
		font = Theme.mono(15, bold: true)
		alignment = .center
		focusRingType = .none
		isEditable = true
		isSelectable = true
		target = self
		action = #selector(entered)
		wantsLayer = true
		layer?.cornerRadius = 8
		layer?.backgroundColor = Theme.well.cgColor
		(cell as? NSTextFieldCell)?.usesSingleLineMode = true
	}
	required init?(coder: NSCoder) { fatalError() }

	@objc private func entered() {
		if let v = Double(stringValue.trimmingCharacters(in: .whitespaces)) {
			value = max(range.lowerBound, min(range.upperBound, v))
			commit?(value)
		}
		stringValue = Self.format(value)
		window?.makeFirstResponder(nil)
	}

	override func scrollWheel(with e: NSEvent) {
		let d = e.scrollingDeltaY
		guard abs(d) > 0.5 else { return }
		value = max(range.lowerBound, min(range.upperBound, value + (d > 0 ? step : -step)))
		commit?(value)
	}
}

final class Meter: NSView {
	var left: Float = 0, right: Float = 0
	var load = "" { didSet { needsDisplay = true } }
	override var isFlipped: Bool { true }

	func set(_ l: Float, _ r: Float) {
		left = max(l, left * 0.85)
		right = max(r, right * 0.85)
		needsDisplay = true
	}

	override func draw(_ dirty: NSRect) {
		let barW: CGFloat = 80
		for (i, v) in [left, right].enumerated() {
			let r = NSRect(x: 0, y: bounds.midY - 5 + CGFloat(i) * 6, width: barW, height: 4)
			Theme.well.setFill()
			r.fill()
			let db = 20 * log10(max(1e-5, Double(v)))
			let f = CGFloat(max(0, min(1, (db + 48) / 48)))
			(v >= 1 ? Theme.red : db > -6 ? Theme.warn : Theme.ok).setFill()
			NSRect(x: r.minX, y: r.minY, width: barW * f, height: 4).fill()
		}
		let t = NSAttributedString.styled(load, Theme.mono(11.5), Theme.muted)
		t.draw(at: NSPoint(x: barW + 12, y: ((bounds.height - t.size().height) / 2).rounded()))
	}
}

// The plugins found, shown where the plugin goes until one is loaded.
final class PluginList: NSView {
	var plugins: [SDPluginInfo] = [] { didSet { needsDisplay = true } }
	var choose: ((SDPluginInfo) -> Void)?
	var message: String? { didSet { needsDisplay = true } }
	private var hoverRow: Int?
	private let rowH: CGFloat = 30
	private let top: CGFloat = 54
	override var isFlipped: Bool { true }

	override init(frame: NSRect) {
		super.init(frame: frame)
		addTrackingArea(NSTrackingArea(rect: .zero, options: [.mouseMoved, .mouseEnteredAndExited, .activeInActiveApp, .inVisibleRect], owner: self))
	}
	required init?(coder: NSCoder) { fatalError() }

	override func draw(_ dirty: NSRect) {
		Theme.panel.setFill()
		bounds.fill()
		NSAttributedString.styled("PICK A PLUGIN", Theme.mono(11, bold: true), Theme.muted, kern: 0.8).draw(at: NSPoint(x: 18, y: 18))
		if let message {
			NSAttributedString.styled(message, Theme.mono(11), Theme.red).draw(in: NSRect(x: 18, y: bounds.height - 60, width: bounds.width - 36, height: 50))
		}
		for (i, p) in plugins.enumerated() {
			let r = NSRect(x: 8, y: top + CGFloat(i) * rowH, width: bounds.width - 16, height: rowH - 2)
			if hoverRow == i {
				Theme.selection.setFill()
				NSBezierPath(roundedRect: r, xRadius: 5, yRadius: 5).fill()
			}
			NSAttributedString.styled(p.name, Theme.mono(12, bold: true), Theme.ink).draw(at: NSPoint(x: r.minX + 10, y: r.minY + 6))
			let kind = p.isEffect ? (p.acceptsMIDI ? "midi fx" : "fx") : "instr"
			let detail = NSAttributedString.styled("\(p.version)  \(kind)", Theme.mono(10.5), Theme.muted)
			detail.draw(at: NSPoint(x: r.maxX - detail.size().width - 10, y: r.minY + 7))
		}
	}

	private func row(_ e: NSEvent) -> Int? {
		let p = convert(e.locationInWindow, from: nil)
		let i = Int(floor((p.y - top) / rowH))
		return p.y >= top && i < plugins.count ? i : nil
	}
	override func mouseMoved(with e: NSEvent) { hoverRow = row(e); needsDisplay = true }
	override func mouseExited(with e: NSEvent) { hoverRow = nil; needsDisplay = true }
	override func mouseDown(with e: NSEvent) { if let i = row(e) { choose?(plugins[i]) } }
	var wantedHeight: CGFloat { top + CGFloat(plugins.count) * rowH + 70 }
}

// The strip across the top: also where the window is dragged from.
final class Bar: NSView {
	override var isFlipped: Bool { true }
	override var mouseDownCanMoveWindow: Bool { true }
	override func draw(_ dirty: NSRect) {
		Theme.panel.setFill()
		bounds.fill()
		Theme.rule.setFill()
		NSRect(x: 0, y: bounds.height - 1, width: bounds.width, height: 1).fill()
	}
}

func label(_ text: String = "", size: CGFloat = 11, colour: NSColor = Theme.muted, bold: Bool = false) -> NSTextField {
	let l = NSTextField(labelWithString: text)
	l.font = Theme.mono(size, bold: bold)
	l.textColor = colour
	l.lineBreakMode = .byTruncatingMiddle
	l.cell?.truncatesLastVisibleLine = true
	return l
}

// The tabs over the plugin box: the plugin, then each effect (a dot to bypass it, × to remove
// it), then "+ FX". Clicking a tab shows that plugin's panel.
final class TabStrip: NSView {
	struct Tab {
		var title: String
		var effect: Bool
		var bypass: Bool
		var missing: Bool
	}
	var tabs: [Tab] = [] { didSet { needsDisplay = true } }
	var selected = 0 { didSet { needsDisplay = true } }
	var select: ((Int) -> Void)?
	var bypass: ((Int) -> Void)?
	var remove: ((Int) -> Void)?
	var addMenu: (() -> NSMenu)?
	private var hover: Int?
	override var isFlipped: Bool { true }

	override init(frame: NSRect) {
		super.init(frame: frame)
		addTrackingArea(NSTrackingArea(rect: .zero, options: [.mouseMoved, .mouseEnteredAndExited, .activeInActiveApp, .inVisibleRect], owner: self))
	}
	required init?(coder: NSCoder) { fatalError() }

	private let font = Theme.mono(12, bold: true)
	private func label(_ t: Tab) -> NSAttributedString {
		.styled(t.title, font, t.missing ? Theme.red : t.bypass ? Theme.muted : Theme.ink)
	}

	// Each tab's rect and, for effects, its bypass dot and × rects; then the add button's.
	private func geometry() -> (tabs: [(NSRect, NSRect?, NSRect?)], add: NSRect) {
		var x: CGFloat = 0
		var out: [(NSRect, NSRect?, NSRect?)] = []
		for t in tabs {
			let w = ceil(label(t).size().width) + (t.effect ? 64 : 28)
			let r = NSRect(x: x, y: 0, width: w, height: bounds.height)
			out.append((r, t.effect ? NSRect(x: r.minX + 10, y: r.midY - 6, width: 12, height: 12) : nil,
			            t.effect ? NSRect(x: r.maxX - 24, y: r.midY - 8, width: 16, height: 16) : nil))
			x += w + 6
		}
		let addW = ceil(NSAttributedString.styled("+ FX  ▾", font, Theme.ink).size().width) + 26
		return (out, NSRect(x: x, y: 0, width: addW, height: bounds.height))
	}

	override func draw(_ dirtyRect: NSRect) {
		let g = geometry()
		for (i, t) in tabs.enumerated() {
			let (r, dot, close) = g.tabs[i]
			let path = NSBezierPath(roundedRect: r.insetBy(dx: 0.5, dy: 0.5), xRadius: 8, yRadius: 8)
			(i == selected ? Theme.selection : hover == i ? Theme.well.blended(withFraction: 0.3, of: Theme.selection)! : Theme.well).setFill()
			path.fill()
			let l = label(t)
			let lx = t.effect ? r.minX + 30 : r.minX + 14
			l.draw(at: NSPoint(x: lx, y: ((bounds.height - l.size().height) / 2).rounded()))
			if let dot {
				(t.bypass ? Theme.muted.withAlphaComponent(0.5) : Theme.ok).setFill()
				NSBezierPath(ovalIn: dot.insetBy(dx: 1.5, dy: 1.5)).fill()
			}
			if let close {
				let x = NSAttributedString.styled("×", Theme.mono(14, bold: true), Theme.muted)
				x.draw(at: NSPoint(x: close.midX - x.size().width / 2, y: close.midY - x.size().height / 2))
			}
		}
		let addLabel = NSAttributedString.styled("+ FX  ▾", font, Theme.accent)
		let a = g.add
		Theme.well.setFill()
		NSBezierPath(roundedRect: a.insetBy(dx: 0.5, dy: 0.5), xRadius: 8, yRadius: 8).fill()
		addLabel.draw(at: NSPoint(x: a.midX - addLabel.size().width / 2, y: ((bounds.height - addLabel.size().height) / 2).rounded()))
	}

	override func mouseMoved(with e: NSEvent) {
		let p = convert(e.locationInWindow, from: nil)
		let h = geometry().tabs.firstIndex { $0.0.contains(p) }
		if h != hover { hover = h; needsDisplay = true }
	}
	override func mouseExited(with e: NSEvent) { hover = nil; needsDisplay = true }
	override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

	override func mouseDown(with e: NSEvent) {
		let p = convert(e.locationInWindow, from: nil)
		let g = geometry()
		if g.add.contains(p), let m = addMenu?() {
			m.appearance = NSAppearance(named: .darkAqua)
			m.popUp(positioning: nil, at: NSPoint(x: g.add.minX, y: g.add.maxY + 4), in: self)
			return
		}
		for (i, (r, dot, close)) in g.tabs.enumerated() where r.contains(p) {
			if let dot, dot.insetBy(dx: -4, dy: -4).contains(p) { bypass?(i) } else if let close, close.insetBy(dx: -3, dy: -3).contains(p) { remove?(i) } else { select?(i) }
			return
		}
	}
}
