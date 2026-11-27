// The drum lanes for each Lemus drum machine, in panel order, from each plugin's own note map,
// with the velocities its sequencer uses for a normal and an accented step. Other plugins get the
// General MIDI drum notes. Notes in the song that no lane plays get a lane of their own.
import Foundation

struct DrumKit {
	let lanes: [DrumLane]
	let normal: Int // a step's velocity
	let accent: Int // an accented step's
}

enum DrumMaps {
	private static func kit(_ lanes: [(String, Int)], normal: Int = 100, accent: Int = 127) -> DrumKit {
		DrumKit(lanes: lanes.map { DrumLane(name: $0.0, note: $0.1) }, normal: normal, accent: accent)
	}

	static let kits: [String: DrumKit] = [
		// machines/tr909/plugin/PluginProcessor.cpp voiceOfNote; velocity sets the accent DAC
		// directly, 50 = the sequencer's normal level and 80 its accent (README).
		"ro-drums-909": kit([("BD", 36), ("SD", 38), ("LT", 43), ("MT", 47), ("HT", 50), ("RIM", 37), ("CLAP", 39),
		                     ("CH", 42), ("OH", 46), ("CRASH", 49), ("RIDE", 51)], normal: 50, accent: 80),
		// machines/tr808 noteVoice; velocity 100 and up sets the common accent.
		"ro-drums-808": kit([("BD", 36), ("SD", 38), ("LT", 41), ("MT", 45), ("HT", 48), ("RS", 37), ("CP", 39),
		                     ("CB", 56), ("CY", 49), ("OH", 46), ("CH", 42)], normal: 90, accent: 127),
		// machines/drumulator kNoteMap (OS 3.0); level = velocity >> 3.
		"em-drums-d": kit([("BASS", 36), ("SNARE", 38), ("RIM", 37), ("HI TOM", 48), ("MID TOM", 45), ("LO TOM", 41),
		                   ("CLAVE", 59), ("COWBELL", 56), ("CLAPS", 39), ("HH OPEN", 46), ("HH CLOSED", 42), ("CYMBAL", 49)]),
		// machines/sp12 kNoteSound (OS 2.6 defaults), by bank.
		"em-drums-12": kit([("BASS 1", 36), ("BASS 2", 38), ("SNARE 1", 40), ("SNARE 2", 41), ("EL SN 1", 43), ("EL SN 2", 45),
		                    ("RIM", 39), ("COWBELL", 37), ("TOM 1", 47), ("TOM 2", 48), ("TOM 3", 50), ("TOM 4", 52),
		                    ("EL TOM 1", 53), ("EL TOM 2", 55), ("EL TOM 3", 57), ("EL TOM 4", 59), ("HIHAT 1", 42),
		                    ("HIHAT 2", 44), ("HIHAT 3", 46), ("CLAPS 1", 49), ("CLAPS 2", 51), ("RIDE 1", 54),
		                    ("RIDE 2", 56), ("CRASH", 58)]),
	]

	static let generalMIDI = kit([("KICK", 36), ("SNARE", 38), ("RIM", 37), ("CLAP", 39), ("LO TOM", 41), ("MID TOM", 45),
	                              ("HI TOM", 48), ("CL HAT", 42), ("OP HAT", 46), ("CRASH", 49), ("RIDE", 51), ("COWBELL", 56)])

	static func kit(for plugin: String, notes: [Note]) -> DrumKit {
		let base = kits[plugin] ?? generalMIDI
		let known = Set(base.lanes.map(\.note))
		let extra = Set(notes.map(\.note)).subtracting(known).sorted().map { DrumLane(name: noteName($0), note: $0) }
		return DrumKit(lanes: base.lanes + extra, normal: base.normal, accent: base.accent)
	}
}
