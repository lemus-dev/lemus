// Drum mode: one lane per instrument, one cell per grid step. Click a cell to set or clear it
// (drag to paint more), Option-click or right-click toggles its accent (velocity 127). Click a
// lane's name to hear it. The notes are the song's own: switching modes loses nothing.
import AppKit

struct DrumLane {
	let name: String
	let note: Int
}

final class DrumGrid: NSView {
	var song = Song()
	var kit = DrumMaps.generalMIDI { didSet { needsDisplay = true } }
	private var lanes: [DrumLane] { kit.lanes }
	private func isAccent(_ vel: Int) -> Bool { vel * 2 >= kit.normal + kit.accent }
	var onChange: ((_ final: Bool) -> Void)?
	var audition: ((_ note: Int, _ on: Bool) -> Void)?
	var playBeat: Double = -1 {
		didSet {
			if oldValue != playBeat, stepAt(oldValue) != stepAt(playBeat) || (oldValue < 0) != (playBeat < 0) { needsDisplay = true }
		}
	}

	private let labelW: CGFloat = 76
	private let rulerH: CGFloat = 18
	private var topLane = 0
	private var before: [Note] = []
	private var paint: (add: Bool, accent: Bool)?
	private var painted = Set<String>()
	private var sounding: Int?
	private var scrollRemainder: CGFloat = 0

	override var isFlipped: Bool { true }
	override var acceptsFirstResponder: Bool { true }
	override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

	// MARK: geometry

	private var steps: Int { max(1, Int((song.loopBeats / song.grid).rounded())) }
	private var cellW: CGFloat { (bounds.width - labelW) / CGFloat(steps) }
	private var rowH: CGFloat {
		max(16, min(28, floor((bounds.height - rulerH) / CGFloat(max(1, lanes.count)))))
	}
	private var visibleLanes: Int { Int(floor((bounds.height - rulerH) / rowH)) }
	private func stepAt(_ beat: Double) -> Int { beat < 0 ? -1 : Int(floor(beat / song.grid + 1e-9)) }
	private func cellRect(lane: Int, step: Int) -> NSRect {
		NSRect(x: labelW + CGFloat(step) * cellW, y: rulerH + CGFloat(lane - topLane) * rowH, width: cellW, height: rowH)
	}
	private func hit(_ p: NSPoint) -> (lane: Int, step: Int)? {
		guard p.y >= rulerH else { return nil }
		let lane = topLane + Int(floor((p.y - rulerH) / rowH))
		guard lane >= 0, lane < lanes.count else { return nil }
		if p.x < labelW { return (lane, -1) }
		let step = Int(floor((p.x - labelW) / cellW))
		return step >= 0 && step < steps ? (lane, step) : nil
	}

	// The note in a cell: its pitch, starting inside the step.
	private func noteIndex(lane: Int, step: Int) -> Int? {
		let start = Double(step) * song.grid
		return song.notes.firstIndex { $0.note == lanes[lane].note && $0.at >= start - 1e-9 && $0.at < start + song.grid - 1e-9 }
	}

	// MARK: drawing

	override func draw(_ dirtyRect: NSRect) {
		let dirty = dirtyRect.intersection(bounds)
		Theme.ground.setFill()
		dirty.fill()
		let playStep = stepAt(playBeat)
		let stepsPerBeat = max(1, Int((1 / song.grid).rounded()))
		let sounding = Set(song.notes.filter { playBeat >= $0.at && playBeat < $0.at + max($0.len, song.grid) }.map(\.note))

		for lane in topLane..<min(lanes.count, topLane + visibleLanes + 1) {
			let row = NSRect(x: 0, y: rulerH + CGFloat(lane - topLane) * rowH, width: bounds.width, height: rowH)
			if lane % 2 == 1 { Theme.panel.withAlphaComponent(0.6).setFill(); row.fill() }
			// the lane's name
			let lit = sounding.contains(lanes[lane].note) || self.sounding == lanes[lane].note
			if lit {
				Theme.accent.withAlphaComponent(0.25).setFill()
				NSRect(x: 0, y: row.minY, width: labelW - 2, height: row.height).fill()
			}
			let name = NSAttributedString.styled(lanes[lane].name, Theme.mono(11, bold: true), lit ? Theme.title : Theme.inkSoft)
			name.draw(at: NSPoint(x: 10, y: row.minY + ((row.height - name.size().height) / 2).rounded()))
			// cells
			for step in 0..<steps {
				let r = cellRect(lane: lane, step: step).insetBy(dx: 2, dy: 2.5)
				guard r.intersects(dirty) else { continue }
				let beatGroup = (step / stepsPerBeat) % 2 == 0
				let path = NSBezierPath(roundedRect: r, xRadius: 3, yRadius: 3)
				if let i = noteIndex(lane: lane, step: step) {
					(isAccent(song.notes[i].vel) ? Theme.warn : Theme.accent).setFill()
				} else {
					(beatGroup ? Theme.well : Theme.well.withAlphaComponent(0.55)).setFill()
				}
				path.fill()
				if step == playStep {
					Theme.title.withAlphaComponent(0.35).setFill()
					path.fill()
				}
			}
		}

		// ruler: bar numbers, beat ticks
		Theme.panel.setFill()
		NSRect(x: 0, y: 0, width: bounds.width, height: rulerH).fill()
		Theme.rule.setFill()
		NSRect(x: 0, y: rulerH - 1, width: bounds.width, height: 1).fill()
		for bar in 0..<song.bars {
			let x = labelW + CGFloat(Double(bar * 4) / song.grid) * cellW
			NSAttributedString.styled("\(bar + 1)", Theme.mono(10, bold: true), Theme.muted).draw(at: NSPoint(x: x + 5, y: 2))
			Theme.muted.withAlphaComponent(0.5).setFill()
			NSRect(x: x, y: 0, width: 1, height: bounds.height).fill()
		}
		if playStep >= 0 {
			Theme.title.setFill()
			NSRect(x: labelW + CGFloat(playStep) * cellW, y: rulerH - 3, width: cellW, height: 2).fill()
		}
	}

	// MARK: editing

	func apply(_ notes: [Note], name: String) {
		let old = song.notes
		song.notes = notes
		window?.undoManager?.registerUndo(withTarget: self) { $0.apply(old, name: name) }
		window?.undoManager?.setActionName(name)
		needsDisplay = true
		onChange?(true)
	}

	private func play(_ n: Int?) {
		if let s = sounding { audition?(s, false) }
		sounding = n
		if let n { audition?(n, true) }
		needsDisplay = true
	}

	private func touch(lane: Int, step: Int) {
		guard let paint, painted.insert("\(lane),\(step)").inserted else { return }
		let i = noteIndex(lane: lane, step: step)
		if paint.accent {
			if let i { song.notes[i].vel = isAccent(song.notes[i].vel) ? kit.normal : kit.accent } else {
				song.notes.append(Note(id: newNoteID(), at: Double(step) * song.grid, len: song.grid, note: lanes[lane].note, vel: kit.accent))
			}
		} else if paint.add, i == nil {
			song.notes.append(Note(id: newNoteID(), at: Double(step) * song.grid, len: song.grid, note: lanes[lane].note, vel: kit.normal))
		} else if !paint.add, let i {
			song.notes.remove(at: i)
		}
		needsDisplay = true
		onChange?(false)
	}

	override func mouseDown(with e: NSEvent) {
		window?.makeFirstResponder(self)
		guard let (lane, step) = hit(convert(e.locationInWindow, from: nil)) else { return }
		if step < 0 {
			play(lanes[lane].note)
			return
		}
		before = song.notes
		painted = []
		let accent = e.modifierFlags.contains(.option)
		paint = (add: noteIndex(lane: lane, step: step) == nil, accent: accent)
		if paint!.add || accent { play(lanes[lane].note) }
		touch(lane: lane, step: step)
	}

	override func rightMouseDown(with e: NSEvent) {
		guard let (lane, step) = hit(convert(e.locationInWindow, from: nil)), step >= 0 else { return }
		before = song.notes
		painted = []
		paint = (add: true, accent: true)
		touch(lane: lane, step: step)
		paint = nil
		finish("Accent")
	}

	override func mouseDragged(with e: NSEvent) {
		guard paint != nil, let (lane, step) = hit(convert(e.locationInWindow, from: nil)), step >= 0 else { return }
		touch(lane: lane, step: step)
	}

	override func mouseUp(with e: NSEvent) {
		play(nil)
		if paint != nil { finish(paint!.accent ? "Accent" : paint!.add ? "Add Steps" : "Clear Steps") }
		paint = nil
	}

	private func finish(_ name: String) {
		guard song.notes != before else { return }
		let now = song.notes
		song.notes = before
		apply(now, name: name)
	}

	override func scrollWheel(with e: NSEvent) {
		guard lanes.count > visibleLanes else { return }
		scrollRemainder -= e.hasPreciseScrollingDeltas ? e.scrollingDeltaY / rowH : e.scrollingDeltaY
		let n = Int(scrollRemainder)
		guard n != 0 else { return }
		scrollRemainder -= CGFloat(n)
		topLane = max(0, min(lanes.count - visibleLanes, topLane + n))
		needsDisplay = true
	}

	override func setFrameSize(_ size: NSSize) {
		super.setFrameSize(size)
		topLane = max(0, min(topLane, lanes.count - visibleLanes))
	}
}
