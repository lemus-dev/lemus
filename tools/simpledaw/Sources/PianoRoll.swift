// The sequence: a piano roll over the whole loop. Click to add a note (drag to make it longer),
// drag a note to move it, its right edge to resize it; double-click or right-click deletes.
// Shift-click adds to the selection, Option-drag selects a box. A toggles accent (velocity 127),
// arrows move the selection (Shift: an octave / a bar), Delete removes it. The keys on the left
// play the plugin.
import AppKit

final class PianoRoll: NSView {
	var song = Song()
	var onChange: ((_ final: Bool) -> Void)? // final: the edit is done (save it)
	var audition: ((_ note: Int, _ on: Bool) -> Void)?
	var playBeat: Double = -1 {
		didSet { if oldValue != playBeat { invalidatePlayhead(oldValue); invalidatePlayhead(playBeat) } }
	}
	private(set) var selected = Set<Int>()
	private var topNote = 76
	private let rowH: CGFloat = 12
	private let keyW: CGFloat = 46
	private let rulerH: CGFloat = 18

	private enum Drag {
		case none
		case move(origin: NSPoint, start: [Int: Note], lead: Int)
		case resize(id: Int)
		case band(origin: NSPoint, base: Set<Int>)
		case key(Int)
	}
	private var drag = Drag.none
	private var band: NSRect?
	private var before: [Note] = []
	private var sounding: Int?
	private var scrollRemainder: CGFloat = 0

	override var isFlipped: Bool { true }
	override var acceptsFirstResponder: Bool { true }
	override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

	// MARK: geometry

	private var beatW: CGFloat { max(1, (bounds.width - keyW) / CGFloat(song.loopBeats)) }
	private func x(_ beat: Double) -> CGFloat { keyW + CGFloat(beat) * beatW }
	private func beat(_ x: CGFloat) -> Double { Double((x - keyW) / beatW) }
	private func y(_ note: Int) -> CGFloat { rulerH + CGFloat(topNote - note) * rowH }
	private func noteAt(_ y: CGFloat) -> Int { max(0, min(127, topNote - Int(floor((y - rulerH) / rowH)))) }
	private func rect(_ n: Note) -> NSRect {
		NSRect(x: x(n.at), y: y(n.note), width: max(4, CGFloat(n.len) * beatW), height: rowH)
	}
	private var visibleRows: Int { Int(ceil((bounds.height - rulerH) / rowH)) }
	private func isBlack(_ n: Int) -> Bool { [1, 3, 6, 8, 10].contains(n % 12) }
	private func snap(_ b: Double) -> Double { (b / song.grid).rounded() * song.grid }
	private func snapDown(_ b: Double) -> Double { floor(b / song.grid + 1e-9) * song.grid }

	private var wantsCentre = true
	private var oldHeight: CGFloat = 0
	override func setFrameSize(_ size: NSSize) {
		super.setFrameSize(size)
		if wantsCentre || size.height != oldHeight, size.height > rulerH + rowH * 4 { centreOnNotes() }
		oldHeight = size.height
	}

	func centreOnNotes() {
		guard bounds.height > rulerH + rowH * 4 else { wantsCentre = true; return }
		wantsCentre = false
		// The notes' range in the middle of the rows that show whole (the lowest kept in view).
		let pitches = song.notes.map(\.note)
		let lo = pitches.min() ?? 48, hi = pitches.max() ?? 60
		let rows = Int(floor((bounds.height - rulerH) / rowH))
		let pad = max(0, (rows - (hi - lo + 1)) / 2)
		topNote = max(min(127, hi + pad), min(127, rows - 1))
		if topNote - lo > rows - 1 { topNote = min(127, lo + rows - 1) }
		needsDisplay = true
	}

	private func invalidatePlayhead(_ b: Double) {
		guard b >= 0 else { return }
		setNeedsDisplay(NSRect(x: x(b) - 3, y: 0, width: 6, height: bounds.height))
		setNeedsDisplay(NSRect(x: 0, y: 0, width: keyW, height: bounds.height))
	}

	// MARK: drawing

	override func draw(_ dirtyRect: NSRect) {
		let dirty = dirtyRect.intersection(bounds)
		Theme.ground.setFill()
		dirty.fill()
		let loop = song.loopBeats
		let gridRight = x(loop)

		// rows
		for i in 0...visibleRows {
			let n = topNote - i
			guard (0...127).contains(n) else { continue }
			let r = NSRect(x: keyW, y: y(n), width: gridRight - keyW, height: rowH)
			guard r.intersects(dirty) else { continue }
			if isBlack(n) { Theme.panel.setFill(); r.fill() }
			if n % 12 == 0 {
				Theme.rule.setFill()
				NSRect(x: keyW, y: r.maxY - 1, width: r.width, height: 1).fill()
			}
		}
		// grid lines: steps faint, beats, bars brighter
		let steps = Int((loop / song.grid).rounded())
		for s in 0...steps {
			let b = Double(s) * song.grid
			let xx = round(x(b))
			let bar = abs(b.truncatingRemainder(dividingBy: 4)) < 1e-9
			let onBeat = abs(b - b.rounded()) < 1e-9
			(bar ? Theme.muted.withAlphaComponent(0.55) : onBeat ? Theme.rule : Theme.rule.withAlphaComponent(0.16)).setFill()
			NSRect(x: xx, y: rulerH, width: 1, height: bounds.height - rulerH).fill()
		}

		// notes
		let sound = soundingNotes()
		for n in song.notes where n.at < loop {
			let r = rect(n).insetBy(dx: 0.5, dy: 1)
			guard r.intersects(dirty) else { continue }
			let base = n.vel >= 120 ? Theme.warn : Theme.accent
			base.withAlphaComponent(0.4 + 0.6 * CGFloat(n.vel) / 127).setFill()
			let path = NSBezierPath(roundedRect: r, xRadius: 2, yRadius: 2)
			path.fill()
			if selected.contains(n.id) {
				Theme.ink.setStroke()
				path.lineWidth = 1.5
				path.stroke()
			}
			if r.width > 24 {
				NSAttributedString.styled(noteName(n.note), Theme.mono(9, bold: true), Theme.onAccent)
					.draw(at: NSPoint(x: r.minX + 3, y: r.minY - 1))
			}
		}
		if let band {
			Theme.accent.withAlphaComponent(0.12).setFill()
			band.fill()
			Theme.accent.setStroke()
			NSBezierPath(rect: band).stroke()
		}

		// keyboard
		for i in 0...visibleRows {
			let n = topNote - i
			guard (0...127).contains(n) else { continue }
			let r = NSRect(x: 0, y: y(n), width: keyW - 1, height: rowH)
			guard r.intersects(dirty) else { continue }
			let lit = sound.contains(n) || sounding == n
			(lit ? Theme.accent : isBlack(n) ? Theme.panel : Theme.well).setFill()
			r.fill()
			Theme.ground.setFill()
			NSRect(x: 0, y: r.maxY - 1, width: keyW, height: 1).fill()
			if n % 12 == 0 || lit {
				NSAttributedString.styled(noteName(n), Theme.mono(9, bold: n % 12 == 0), lit ? Theme.onAccent : Theme.muted)
					.draw(at: NSPoint(x: 6, y: r.minY - 1))
			}
		}

		// ruler
		let ruler = NSRect(x: 0, y: 0, width: bounds.width, height: rulerH)
		if ruler.intersects(dirty) {
			Theme.panel.setFill()
			ruler.fill()
			Theme.rule.setFill()
			NSRect(x: 0, y: rulerH - 1, width: bounds.width, height: 1).fill()
			for bar in 0..<song.bars {
				NSAttributedString.styled("\(bar + 1)", Theme.mono(10, bold: true), Theme.muted)
					.draw(at: NSPoint(x: x(Double(bar * 4)) + 5, y: 2))
			}
		}

		// playhead
		if playBeat >= 0 {
			let px = round(x(playBeat))
			Theme.title.setFill()
			NSRect(x: px, y: 0, width: 1.5, height: bounds.height).fill()
		}
	}

	private func soundingNotes() -> Set<Int> {
		guard playBeat >= 0 else { return [] }
		return Set(song.notes.filter { playBeat >= $0.at && playBeat < $0.at + $0.len }.map(\.note))
	}

	// MARK: editing

	private func hit(_ p: NSPoint) -> Note? {
		song.notes.last { $0.at < song.loopBeats && rect($0).contains(p) }
	}

	private func index(_ id: Int) -> Int? { song.notes.firstIndex { $0.id == id } }

	// Replaces the notes as one undoable step.
	func apply(_ notes: [Note], name: String) {
		let old = song.notes
		song.notes = notes
		window?.undoManager?.registerUndo(withTarget: self) { $0.apply(old, name: name) }
		window?.undoManager?.setActionName(name)
		selected = selected.intersection(Set(notes.map(\.id)))
		needsDisplay = true
		onChange?(true)
	}

	private func finishDrag(_ name: String) {
		guard song.notes != before else { return }
		let now = song.notes
		song.notes = before
		apply(now, name: name)
	}

	private func play(_ n: Int?) {
		if let s = sounding { audition?(s, false) }
		sounding = n
		if let n { audition?(n, true) }
		setNeedsDisplay(NSRect(x: 0, y: 0, width: keyW, height: bounds.height))
	}

	override func mouseDown(with e: NSEvent) {
		window?.makeFirstResponder(self)
		let p = convert(e.locationInWindow, from: nil)
		guard p.y >= rulerH else { return }
		if p.x < keyW {
			let n = noteAt(p.y)
			play(n)
			drag = .key(n)
			return
		}
		before = song.notes
		let shift = e.modifierFlags.contains(.shift)
		if let n = hit(p) {
			if e.clickCount == 2 {
				apply(song.notes.filter { $0.id != n.id }, name: "Delete Note")
				drag = .none
				return
			}
			if shift {
				if selected.contains(n.id) { selected.remove(n.id) } else { selected.insert(n.id) }
				needsDisplay = true
				drag = .none
				return
			}
			if !selected.contains(n.id) { selected = [n.id] }
			if p.x > rect(n).maxX - 6 {
				drag = .resize(id: n.id)
			} else {
				var start: [Int: Note] = [:]
				for s in song.notes where selected.contains(s.id) { start[s.id] = s }
				drag = .move(origin: p, start: start, lead: n.id)
				play(n.note)
			}
			needsDisplay = true
			return
		}
		if e.modifierFlags.contains(.option) || e.modifierFlags.contains(.command) {
			drag = .band(origin: p, base: shift ? selected : [])
			if !shift { selected = [] }
			needsDisplay = true
			return
		}
		let at = snapDown(beat(p.x))
		guard at >= 0, at < song.loopBeats else { return }
		let n = Note(id: newNoteID(), at: at, len: song.grid, note: noteAt(p.y), vel: 100)
		song.notes.append(n)
		selected = [n.id]
		drag = .resize(id: n.id)
		play(n.note)
		needsDisplay = true
		onChange?(false)
	}

	override func mouseDragged(with e: NSEvent) {
		let p = convert(e.locationInWindow, from: nil)
		switch drag {
		case .key:
			let n = noteAt(p.y)
			if n != sounding { play(n) }
		case let .move(origin, start, lead):
			let dBeat = ((p.x - origin.x) / beatW / CGFloat(song.grid)).rounded() * CGFloat(song.grid)
			let dNote = Int(((origin.y - p.y) / rowH).rounded())
			for (id, s) in start {
				guard let i = index(id) else { continue }
				song.notes[i].at = max(0, min(song.loopBeats - song.grid, s.at + Double(dBeat)))
				song.notes[i].note = max(0, min(127, s.note + dNote))
			}
			if let i = index(lead), song.notes[i].note != sounding { play(song.notes[i].note) }
			needsDisplay = true
			onChange?(false)
		case let .resize(id):
			guard let i = index(id) else { return }
			let end = max(song.notes[i].at + song.grid, snap(beat(p.x)))
			song.notes[i].len = min(end, song.loopBeats + song.grid) - song.notes[i].at
			needsDisplay = true
			onChange?(false)
		case let .band(origin, base):
			let r = NSRect(x: min(origin.x, p.x), y: min(origin.y, p.y), width: abs(p.x - origin.x), height: abs(p.y - origin.y))
			band = r
			selected = base.union(song.notes.filter { rect($0).intersects(r) }.map(\.id))
			needsDisplay = true
		case .none:
			break
		}
	}

	override func mouseUp(with e: NSEvent) {
		play(nil)
		switch drag {
		case .move: finishDrag("Move")
		case .resize: finishDrag(before.count < song.notes.count ? "Add Note" : "Resize")
		case .band: band = nil; needsDisplay = true
		default: break
		}
		drag = .none
	}

	override func rightMouseDown(with e: NSEvent) {
		let p = convert(e.locationInWindow, from: nil)
		if let n = hit(p) { apply(song.notes.filter { $0.id != n.id }, name: "Delete Note") }
	}

	override func scrollWheel(with e: NSEvent) {
		scrollRemainder += e.hasPreciseScrollingDeltas ? e.scrollingDeltaY / rowH : e.scrollingDeltaY
		let rows = Int(scrollRemainder)
		guard rows != 0 else { return }
		scrollRemainder -= CGFloat(rows)
		topNote = max(min(127, visibleRows - 1), min(127, topNote + rows))
		needsDisplay = true
	}

	override func keyDown(with e: NSEvent) {
		let shift = e.modifierFlags.contains(.shift)
		switch e.keyCode {
		case 51, 117: delete(nil)
		case 123: moveSelection(beats: -(shift ? 4 : song.grid), notes: 0)
		case 124: moveSelection(beats: shift ? 4 : song.grid, notes: 0)
		case 125: moveSelection(beats: 0, notes: shift ? -12 : -1)
		case 126: moveSelection(beats: 0, notes: shift ? 12 : 1)
		default:
			if e.charactersIgnoringModifiers?.lowercased() == "a", !e.modifierFlags.contains(.command) {
				toggleAccent()
			} else {
				super.keyDown(with: e)
			}
		}
	}

	@objc func delete(_ sender: Any?) {
		guard !selected.isEmpty else { return }
		apply(song.notes.filter { !selected.contains($0.id) }, name: "Delete")
		selected = []
	}

	override func selectAll(_ sender: Any?) {
		selected = Set(song.notes.map(\.id))
		needsDisplay = true
	}

	private func moveSelection(beats: Double, notes: Int) {
		guard !selected.isEmpty else { return }
		apply(song.notes.map { n in
			guard selected.contains(n.id) else { return n }
			var m = n
			m.at = max(0, min(song.loopBeats - song.grid, n.at + beats))
			m.note = max(0, min(127, n.note + notes))
			return m
		}, name: "Move")
	}

	private func toggleAccent() {
		let chosen = song.notes.filter { selected.contains($0.id) }
		guard !chosen.isEmpty else { return }
		let on = !chosen.allSatisfy { $0.vel >= 120 }
		apply(song.notes.map { n in
			guard selected.contains(n.id) else { return n }
			var m = n
			m.vel = on ? 127 : 100
			return m
		}, name: "Accent")
	}

	func clearSelection() { selected = [] }
}
