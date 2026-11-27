// A song: one plugin and one looped sequence, as JSON (the Lemus scene format's fields, so a
// scene's notes open here too). Note names are Onyx's: C-3 = MIDI 48.
import Foundation

struct Note: Equatable {
	var id: Int
	var at: Double  // beats
	var len: Double // beats
	var note: Int
	var vel: Int
}

private var noteIDs = 0
func newNoteID() -> Int {
	noteIDs += 1
	return noteIDs
}

func noteName(_ n: Int) -> String {
	let names = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"]
	return n < 12 ? "\(n)" : names[n % 12] + String(n / 12 - 1)
}

func parseNote(_ v: Any?) -> Int? {
	if let n = v as? NSNumber { return n.intValue }
	guard let s = (v as? String)?.trimmingCharacters(in: .whitespaces), let first = s.first else { return nil }
	let letters: [Character: Int] = ["C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11]
	guard var semi = letters[Character(first.uppercased())] else { return Int(s) }
	var rest = s.dropFirst()
	if rest.first == "#" { semi += 1; rest = rest.dropFirst() } else if rest.first == "b" { semi -= 1; rest = rest.dropFirst() } else if rest.first == "-" { rest = rest.dropFirst() }
	guard let octave = Int(rest) else { return nil }
	return 12 * (octave + 1) + semi
}

struct EffectSlot: Equatable {
	var plugin: String
	var bypass = false
}

final class Song {
	var bpm: Double = 120
	var bars: Int = 2
	var grid: Double = 0.25
	var plugin: String?
	var mode: String? // "drums" or "keys"; unset: drums for a plugin named *-drums-*
	var effects: [EffectSlot] = [] // after the plugin, in order
	var notes: [Note] = []
	var extra: [String: Any] = [:] // other fields, kept as they were

	var loopBeats: Double { Double(bars) * 4 }

	static func load(_ url: URL) throws -> Song {
		let data = try Data(contentsOf: url)
		guard let json = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
			throw NSError(domain: "Simple DAW", code: 1, userInfo: [NSLocalizedDescriptionKey: "not a JSON object"])
		}
		let s = Song()
		var rest = json
		if let v = json["bpm"] as? NSNumber { s.bpm = v.doubleValue }
		if let v = json["bars"] as? NSNumber { s.bars = max(1, v.intValue) } else if let v = json["beats"] as? NSNumber { s.bars = max(1, Int(ceil(v.doubleValue / 4))) }
		if let v = json["grid"] as? NSNumber, v.doubleValue > 0 { s.grid = v.doubleValue }
		if let m = json["mode"] as? String { s.mode = m }
		for e in json["effects"] as? [Any] ?? [] {
			if let name = e as? String { s.effects.append(EffectSlot(plugin: name)) } else if let d = e as? [String: Any], let name = d["plugin"] as? String {
				s.effects.append(EffectSlot(plugin: name, bypass: (d["bypass"] as? Bool) ?? false))
			}
		}
		if let p = json["plugin"] as? String {
			s.plugin = p
		} else if let sources = json["sources"] as? [[String: Any]], let p = sources.first?["plugin"] as? String {
			s.plugin = p.hasPrefix("au:") ? String(p.dropFirst(3)) : p
		}
		for n in json["notes"] as? [[String: Any]] ?? [] {
			guard let pitch = parseNote(n["note"]), (0...127).contains(pitch) else { continue }
			let vel = (n["vel"] as? NSNumber)?.intValue ?? ((n["accent"] as? Bool) == true ? 127 : 100)
			s.notes.append(Note(id: newNoteID(), at: (n["at"] as? NSNumber)?.doubleValue ?? 0,
			                    len: (n["len"] as? NSNumber)?.doubleValue ?? 0.25, note: pitch, vel: max(1, min(127, vel))))
		}
		for k in ["bpm", "bars", "beats", "grid", "plugin", "mode", "effects", "notes"] { rest[k] = nil }
		s.extra = rest
		return s
	}

	// One note per line, so the file reads (and diffs) well.
	func json() -> String {
		func num(_ v: Double) -> String {
			v == v.rounded() && abs(v) < 1e9 ? String(Int(v)) : String(format: "%.6g", v)
		}
		func str(_ s: String) -> String {
			let d = try? JSONSerialization.data(withJSONObject: s, options: [.fragmentsAllowed])
			return d.flatMap { String(data: $0, encoding: .utf8) } ?? "\"\""
		}
		var lines = ["{"]
		if let p = plugin { lines.append("  \"plugin\": \(str(p)),") }
		if let m = mode { lines.append("  \"mode\": \(str(m)),") }
		if !effects.isEmpty {
			lines.append("  \"effects\": [")
			for (i, e) in effects.enumerated() {
				lines.append("    {\"plugin\": \(str(e.plugin)), \"bypass\": \(e.bypass)}" + (i < effects.count - 1 ? "," : ""))
			}
			lines.append("  ],")
		}
		lines.append("  \"bpm\": \(num(bpm)),")
		lines.append("  \"bars\": \(bars),")
		lines.append("  \"grid\": \(num(grid)),")
		for k in extra.keys.sorted() {
			if let d = try? JSONSerialization.data(withJSONObject: extra[k]!, options: [.fragmentsAllowed, .sortedKeys]),
			   let s = String(data: d, encoding: .utf8) {
				lines.append("  \(str(k)): \(s),")
			}
		}
		let sorted = notes.sorted { ($0.at, $0.note) < ($1.at, $1.note) }
		if sorted.isEmpty {
			lines.append("  \"notes\": []")
		} else {
			lines.append("  \"notes\": [")
			for (i, n) in sorted.enumerated() {
				lines.append("    {\"at\": \(num(n.at)), \"len\": \(num(n.len)), \"note\": \"\(noteName(n.note))\", \"vel\": \(n.vel)}" + (i < sorted.count - 1 ? "," : ""))
			}
			lines.append("  ]")
		}
		lines.append("}")
		return lines.joined(separator: "\n") + "\n"
	}
}
