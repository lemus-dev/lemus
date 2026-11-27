// The window and everything behind it: the song file (saved on every edit, reloaded when
// something else changes it), the plugin (reloaded when its bundle changes), the transport.
import AppKit

private let barH: CGFloat = 58
private let footerH: CGFloat = 26
private let margin: CGFloat = 16 // round the roll and the plugin
private let gap: CGFloat = 12    // between them
private let pluginScale: CGFloat = 0.9
private let rollDefault: CGFloat = 300
private let barMinWidth: CGFloat = 1060
private let tabsH: CGFloat = 30

// An effect after the plugin: its loaded copy, its panel, and what the reload watch compares.
final class EffectUnit {
	let info: SDPluginInfo
	let plugin: SDLoadedPlugin
	let view: NSView?
	var size: NSSize
	var stamp: String
	var pending = ""
	init(info: SDPluginInfo, plugin: SDLoadedPlugin, view: NSView?) {
		self.info = info
		self.plugin = plugin
		self.view = view
		size = view?.frame.size ?? .zero
		stamp = info.stamp
	}
}
private let supportDir = FileManager.default.homeDirectoryForCurrentUser
	.appendingPathComponent("Library/Application Support/Simple DAW")

final class FlippedView: NSView {
	var background: NSColor?
	var onResize: (() -> Void)?
	override var isFlipped: Bool { true }
	override func resizeSubviews(withOldSize oldSize: NSSize) { onResize?() }
	override func draw(_ dirty: NSRect) {
		if let background { background.setFill(); dirty.intersection(bounds).fill() }
	}
}

final class AppController: NSObject, NSApplicationDelegate, NSWindowDelegate {
	private let engine = SDEngine()
	private var window: NSWindow!
	private let root = FlippedView()
	private let bar = Bar()
	private let roll = PianoRoll()
	private let drums = DrumGrid()
	private let modePill = Pill("Keys")
	private let host = FlippedView()   // the plugin's box: border, rounded corners
	private let scaler = FlippedView() // shows the plugin at pluginScale
	private let tabs = TabStrip()
	private var fx: [EffectUnit?] = [] // one per song.effects (nil: not found)
	private var shown = 0              // the panel in the box: 0 the plugin, n effect n
	private let list = PluginList()
	private let footer = FlippedView()
	private let footerText = label("", size: 11)
	private let titleLabel = label("Simple DAW", size: 15, colour: Theme.title, bold: true)
	private let pluginPill = Pill("no plugin")
	private let play = PlayButton()
	private let bpmLabel = label("BPM", size: 12, bold: true)
	private let bpm = NumberField()
	private let barsPill = Pill("2 bars")
	private let gridPill = Pill("1/16")
	private let statusLabel = label("", size: 11, bold: true)
	private let meter = Meter()
	private let undo = UndoManager()

	private var song = Song()
	private var songURL = supportDir.appendingPathComponent("song.json")
	private var songMTime: Date?
	private var states: [String: Data] = [:]
	private var plugins: [SDPluginInfo] = []
	private var loaded: SDLoadedPlugin?
	private var pluginView: NSView?
	private var pluginSize = NSSize.zero
	private var rollHeight = rollDefault // the roll's height; changes only when you resize the window
	private var loadedStamp = ""
	private var pendingStamp = ""
	private var reloads = 0
	private var saveWork: DispatchWorkItem?
	private var statusWork: DispatchWorkItem?
	private var ticks = 0
	private var ready = false
	private var pendingOpen: [URL] = []
	private var playOnLaunch = false
	private var background = false
	private var startTab = 0

	// MARK: launch

	func applicationDidFinishLaunching(_ note: Notification) {
		Theme.registerFonts()
		try? FileManager.default.createDirectory(at: supportDir, withIntermediateDirectories: true)
		SDLoadedPlugin.removeStaleCopies()
		buildMenu()
		buildWindow()
		var err: NSString?
		if !engine.start(&err) { status("audio: \(err ?? "failed")", Theme.red) }
		plugins = SDPluginInfo.scan()
		list.plugins = plugins

		var args = Array(CommandLine.arguments.dropFirst())
		while !args.isEmpty {
			let a = args.removeFirst()
			if a == "--play" { playOnLaunch = true } else if a == "--background" { background = true } else if a == "--tab", !args.isEmpty { startTab = Int(args.removeFirst()) ?? 0 } else if !a.hasPrefix("-") { pendingOpen.append(URL(fileURLWithPath: a)) }
		}
		ready = true
		let last = UserDefaults.standard.string(forKey: "song").map { URL(fileURLWithPath: $0) }
		if pendingOpen.isEmpty { openSong(last ?? songURL) } else { open(pendingOpen) }
		pendingOpen = []

		Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in self?.tick() }
		let ui = Timer(timeInterval: 1.0 / 60, repeats: true) { [weak self] _ in self?.frame() }
		RunLoop.main.add(ui, forMode: .common)
		NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] e in
			guard let self, e.keyCode == 49, e.modifierFlags.intersection([.command, .control, .option]).isEmpty,
			      !(self.window.firstResponder is NSTextView) else { return e }
			self.togglePlay()
			return nil
		}
		if background { window.orderFront(nil) } else {
			window.makeKeyAndOrderFront(nil)
			NSApp.activate(ignoringOtherApps: true)
		}
		if playOnLaunch { togglePlay() }
		if startTab > 0, startTab <= fx.count {
			shown = startTab
			refreshShown()
			fitWindow()
		}
	}

	func application(_ app: NSApplication, open urls: [URL]) {
		if ready { open(urls) } else { pendingOpen += urls }
	}

	private func open(_ urls: [URL]) {
		for u in urls {
			if u.pathExtension == "component" {
				if let info = SDPluginInfo.infos(forBundle: u.path, named: u.path).first { switchPlugin(to: info) }
			} else {
				openSong(u)
			}
		}
	}

	func applicationShouldTerminateAfterLastWindowClosed(_ app: NSApplication) -> Bool { true }

	func applicationWillTerminate(_ note: Notification) {
		saveWork?.perform()
		saveStates()
		engine.playing = false
		engine.setUnit(nil, channels: 2, effect: false, midi: false)
		engine.setEffects(nil, channels: nil, bypass: nil, count: 0)
		pluginView?.removeFromSuperview()
		pluginView = nil
		loaded?.dispose()
		for case let u? in fx {
			u.view?.removeFromSuperview()
			u.plugin.dispose()
		}
		writeStatus(running: false)
	}

	func windowWillReturnUndoManager(_ window: NSWindow) -> UndoManager? { undo }

	func windowDidEndLiveResize(_ note: Notification) { rollHeight = max(100, roll.frame.height) }

	// MARK: window

	private func buildWindow() {
		window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1100, height: 560),
		                  styleMask: [.titled, .closable, .miniaturizable, .resizable, .fullSizeContentView],
		                  backing: .buffered, defer: false)
		window.titlebarAppearsTransparent = true
		window.titleVisibility = .hidden
		window.title = "Simple DAW"
		window.appearance = NSAppearance(named: .darkAqua)
		window.backgroundColor = Theme.ground
		window.delegate = self
		window.isReleasedWhenClosed = false
		window.setFrameAutosaveName("Simple DAW main")
		window.contentView = root
		root.background = Theme.ground
		root.onResize = { [weak self] in self?.layout() }

		root.addSubview(roll)
		root.addSubview(drums)
		root.addSubview(tabs)
		root.addSubview(host)
		root.addSubview(bar)
		root.addSubview(footer)
		host.background = Theme.panel
		host.addSubview(list)
		host.addSubview(scaler)
		for v in [host, roll, drums] as [NSView] {
			v.wantsLayer = true
			v.layer?.cornerRadius = 10
			v.layer?.masksToBounds = true
			v.layer?.borderWidth = 1
			v.layer?.borderColor = Theme.rule.cgColor
		}
		footer.background = Theme.panel
		footer.addSubview(footerText)
		for v in [titleLabel, pluginPill, play, bpmLabel, bpm, barsPill, gridPill, modePill, meter] as [NSView] { bar.addSubview(v) }
		footer.addSubview(statusLabel)
		statusLabel.alignment = .right
		statusLabel.lineBreakMode = .byTruncatingTail
		// The macOS 14 SDK stopped clipping views to their bounds; these draw edge to edge.
		for v in [root, roll, drums, host, list, bar, footer, pluginPill, play, barsPill, gridPill, modePill, meter] as [NSView] { v.clipsToBounds = true }

		pluginPill.menu_ = { [weak self] in self?.pluginMenu() ?? NSMenu() }
		barsPill.menu_ = { [weak self] in self?.barsMenu() ?? NSMenu() }
		gridPill.menu_ = { [weak self] in self?.gridMenu() ?? NSMenu() }
		play.action = { [weak self] in self?.togglePlay() }
		bpm.range = 20...300
		bpm.commit = { [weak self] v in self?.setBPM(v) }
		list.choose = { [weak self] info in self?.switchPlugin(to: info) }
		roll.onChange = { [weak self] final in self?.notesChanged(final: final) }
		roll.audition = { [weak self] n, on in self?.engine.sendMIDI(on ? 0x90 : 0x80, data1: UInt8(n), data2: on ? 100 : 0) }
		drums.onChange = roll.onChange
		drums.audition = roll.audition
		modePill.action = { [weak self] in self?.toggleMode() }
		tabs.select = { [weak self] i in
			self?.shown = i
			self?.refreshShown()
			self?.fitWindow()
		}
		tabs.bypass = { [weak self] i in if i > 0 { self?.toggleBypass(i - 1) } }
		tabs.remove = { [weak self] i in if i > 0 { self?.removeEffect(i - 1) } }
		tabs.addMenu = { [weak self] in self?.effectMenu() ?? NSMenu() }
		NotificationCenter.default.addObserver(forName: NSView.frameDidChangeNotification, object: nil, queue: .main) { [weak self] n in
			guard let self, let v = n.object as? NSView else { return }
			if v === self.pluginView, v.frame.size != self.pluginSize {
				self.pluginSize = v.frame.size
				self.fitWindow()
			} else if let u = self.fx.first(where: { $0?.view === v }) ?? nil, v.frame.size != u.size {
				u.size = v.frame.size
				self.fitWindow()
			}
		}
		window.center()
	}

	private func scaled(_ s: NSSize) -> NSSize {
		NSSize(width: (s.width * pluginScale).rounded(), height: (s.height * pluginScale).rounded())
	}

	// The box is as wide as the widest panel of the plugin and its effects (so the window keeps
	// its width), and as tall as the one shown.
	private var pluginArea: NSSize {
		var w = pluginView != nil ? scaled(pluginSize).width : 560
		for case let u? in fx where u.view != nil { w = max(w, scaled(u.size).width) }
		let h = shownView != nil ? scaled(shownSize).height : shown == 0 ? max(420, list.wantedHeight) : 300
		return NSSize(width: w, height: h)
	}

	private var shownView: NSView? { shown == 0 ? pluginView : shown - 1 < fx.count ? fx[shown - 1]?.view : nil }
	private var shownSize: NSSize { shown == 0 ? pluginSize : shown - 1 < fx.count ? fx[shown - 1]?.size ?? .zero : .zero }

	// Puts the chosen tab's panel in the box.
	private func refreshShown() {
		if shown > fx.count { shown = 0 }
		let v = shownView
		for sub in scaler.subviews where sub !== v { sub.removeFromSuperview() }
		if let v, v.superview !== scaler {
			v.setFrameOrigin(.zero)
			scaler.addSubview(v)
		}
		scaler.isHidden = v == nil
		list.isHidden = !(shown == 0 && pluginView == nil)
		updateTabs()
		layout()
	}

	private func updateTabs() {
		var t = [TabStrip.Tab(title: loaded?.info.shortName ?? "no plugin", effect: false, bypass: false, missing: false)]
		for (i, e) in song.effects.enumerated() {
			let u = i < fx.count ? fx[i] : nil
			t.append(TabStrip.Tab(title: u?.info.shortName ?? e.plugin, effect: true, bypass: e.bypass, missing: u == nil))
		}
		tabs.tabs = t
		tabs.selected = shown
	}

	private func layout() {
		let b = root.bounds
		bar.frame = NSRect(x: 0, y: 0, width: b.width, height: barH)
		footer.frame = NSRect(x: 0, y: b.height - footerH, width: b.width, height: footerH)
		footerText.sizeToFit()
		statusLabel.sizeToFit()
		let fh = footerText.frame.height
		let sw = min(statusLabel.frame.width, (b.width - 32) * 0.55)
		statusLabel.frame = NSRect(x: b.width - 16 - sw, y: ((footerH - fh) / 2).rounded(), width: sw, height: fh)
		footerText.frame = NSRect(x: 16, y: ((footerH - fh) / 2).rounded(), width: max(0, b.width - 32 - sw - 16), height: fh)
		// The roll above the plugin, both the plugin's width, centred.
		let area = pluginArea
		let x0 = max(margin, ((b.width - area.width) / 2).rounded())
		let pluginY = b.height - footerH - margin - area.height
		host.frame = NSRect(x: x0, y: pluginY, width: area.width, height: area.height)
		let tabsY = pluginY - 8 - tabsH
		tabs.frame = NSRect(x: x0, y: tabsY, width: area.width, height: tabsH)
		let rollY = barH + margin
		roll.frame = NSRect(x: x0, y: rollY, width: area.width, height: max(60, tabsY - gap - rollY))
		drums.frame = roll.frame
		list.frame = host.bounds
		if let v = shownView {
			let full = shownSize, sz = scaled(full)
			scaler.frame = NSRect(x: ((area.width - sz.width) / 2).rounded(), y: 0, width: sz.width, height: sz.height)
			scaler.setBoundsSize(full)
			if v.frame.origin != .zero { v.setFrameOrigin(.zero) }
		}

		var x: CGFloat = 84
		let h: CGFloat = 34
		func place(_ v: NSView, _ w: CGFloat, _ height: CGFloat = h, gap: CGFloat = 10) {
			v.frame = NSRect(x: x, y: ((barH - height) / 2).rounded(), width: w, height: height)
			x += w + gap
		}
		titleLabel.sizeToFit()
		place(titleLabel, titleLabel.frame.width, titleLabel.frame.height, gap: 22)
		place(pluginPill, min(320, pluginPill.intrinsicContentSize.width), gap: 16)
		place(play, 48, gap: 16)
		bpmLabel.sizeToFit()
		place(bpmLabel, bpmLabel.frame.width, bpmLabel.frame.height, gap: 8)
		place(bpm, 70, gap: 16)
		place(barsPill, barsPill.intrinsicContentSize.width)
		place(gridPill, gridPill.intrinsicContentSize.width)
		place(modePill, modePill.intrinsicContentSize.width, gap: 18)
		let meterW: CGFloat = 250
		meter.frame = NSRect(x: b.width - meterW - 14, y: ((barH - 30) / 2).rounded(), width: meterW, height: 30)
	}

	// Sizes the window to the plugin (its width fixed to it; taller gives the roll more room),
	// keeping the roll's height and the window's top edge.
	private func fitWindow() {
		let area = pluginArea
		let rollH = rollHeight
		let chrome = barH + margin + gap + tabsH + 8 + area.height + margin + footerH
		var size = NSSize(width: max(barMinWidth, area.width + 2 * margin), height: chrome + rollH)
		if let screen = (window.screen ?? NSScreen.main)?.visibleFrame { size.height = min(size.height, screen.height) }
		window.contentMinSize = NSSize(width: size.width, height: chrome + 100)
		window.contentMaxSize = NSSize(width: size.width, height: 10000)
		var f = window.frame
		let top = f.maxY
		f.size = window.frameRect(forContentRect: NSRect(origin: .zero, size: size)).size
		f.origin.y = top - f.height
		window.setFrame(f, display: true)
		layout()
	}

	// MARK: song

	private func openSong(_ url: URL) {
		if loaded != nil || !fx.isEmpty { saveStates() }
		saveWork?.perform()
		disposeEffects(fx.compactMap { $0 }) // this song's effects load with its own states
		fx = []
		pushChain()
		shown = 0
		songURL = url
		if FileManager.default.fileExists(atPath: url.path) {
			do { song = try Song.load(url) } catch {
				status("can't read \(url.lastPathComponent): \(error.localizedDescription)", Theme.red)
				song = Song()
			}
		} else {
			song = Song()
			song.plugin = loaded?.info.name
			try? FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
			saveSong()
		}
		songMTime = mtime(url)
		UserDefaults.standard.set(url.path, forKey: "song")
		states = [:]
		if let d = try? Data(contentsOf: statesURL),
		   let p = try? PropertyListSerialization.propertyList(from: d, format: nil) as? [String: Data] { states = p }
		undo.removeAllActions()
		roll.song = song
		drums.song = song
		roll.clearSelection()
		roll.centreOnNotes()
		syncControls()
		pushNotes()
		if let name = song.plugin {
			if let info = pluginInfo(named: name) {
				if loaded?.info.name != info.name || loaded == nil { switchPlugin(to: info, saveOld: false) }
			} else {
				status("plugin \(name) not found", Theme.warn)
			}
		}
		syncEffects()
		window.title = "Simple DAW — \(url.lastPathComponent)"
		updateFooter()
		writeStatus()
	}

	private var statesURL: URL { songURL.deletingPathExtension().appendingPathExtension("states.plist") }

	private func pluginInfo(named name: String) -> SDPluginInfo? {
		if name.hasPrefix("/") { return SDPluginInfo.infos(forBundle: name, named: name).first }
		return plugins.first { $0.name == name } ?? plugins.first { $0.name.lowercased() == name.lowercased() }
	}

	private func syncControls() {
		bpm.value = song.bpm
		engine.bpm = song.bpm
		barsPill.title = "\(song.bars) bar" + (song.bars == 1 ? "" : "s")
		gridPill.title = gridName(song.grid)
		applyMode()
		layout()
	}

	private func pushNotes() {
		let ns = song.notes.map { SDNote(at: $0.at, len: $0.len, note: Int32($0.note), vel: Int32($0.vel)) }
		ns.withUnsafeBufferPointer { engine.setNotes($0.baseAddress, count: $0.count, loopBeats: song.loopBeats) }
	}

	private func notesChanged(final: Bool) {
		pushNotes()
		roll.needsDisplay = true
		drums.needsDisplay = true
		if final { scheduleSave() }
	}

	private func scheduleSave() {
		saveWork?.cancel()
		let w = DispatchWorkItem { [weak self] in self?.saveSong() }
		saveWork = w
		DispatchQueue.main.asyncAfter(deadline: .now() + 0.3, execute: w)
	}

	private func saveSong() {
		saveWork = nil
		do {
			try song.json().write(to: songURL, atomically: true, encoding: .utf8)
			songMTime = mtime(songURL)
		} catch {
			status("can't save: \(error.localizedDescription)", Theme.red)
		}
		writeStatus()
	}

	// Something else wrote the song file: take its notes, tempo, length and plugin.
	private func songChangedOnDisk() {
		guard let s = try? Song.load(songURL) else {
			status("\(songURL.lastPathComponent) changed but doesn't read as a song", Theme.red)
			return
		}
		song = s
		roll.song = s
		drums.song = s
		roll.clearSelection()
		undo.removeAllActions()
		roll.needsDisplay = true
		syncControls()
		pushNotes()
		if let name = s.plugin, name != loaded?.info.name, let info = pluginInfo(named: name) { switchPlugin(to: info) }
		syncEffects()
		status("song reloaded from disk (\(s.notes.count) notes)", Theme.ok)
		writeStatus()
	}

	private func setBPM(_ v: Double) {
		song.bpm = v
		engine.bpm = v
		scheduleSave()
	}

	private func setBars(_ n: Int) {
		song.bars = n
		syncControls()
		pushNotes()
		roll.needsDisplay = true
		drums.needsDisplay = true
		scheduleSave()
	}

	private func setGrid(_ g: Double) {
		song.grid = g
		syncControls()
		roll.needsDisplay = true
		drums.needsDisplay = true
		scheduleSave()
	}

	private func gridName(_ g: Double) -> String {
		let names: [(Double, String)] = [(1, "1/4"), (0.5, "1/8"), (0.25, "1/16"), (0.125, "1/32"), (1.0 / 3, "1/8T"), (1.0 / 6, "1/16T")]
		return names.first { abs($0.0 - g) < 1e-6 }?.1 ?? String(format: "%.3g", g)
	}

	// MARK: drum mode

	private var drumMode: Bool {
		song.mode.map { $0 == "drums" } ?? (loaded?.info.shortName.contains("-drums-") ?? false)
	}

	private func applyMode() {
		let d = drumMode
		roll.isHidden = d
		drums.isHidden = !d
		modePill.title = d ? "Drums" : "Keys"
		modePill.highlighted = false
		if d { drums.kit = DrumMaps.kit(for: loaded?.info.shortName ?? song.plugin ?? "", notes: song.notes) }
		layout()
	}

	private func toggleMode() {
		song.mode = drumMode ? "keys" : "drums"
		applyMode()
		roll.centreOnNotes()
		scheduleSave()
	}

	private func togglePlay() {
		engine.playing = !engine.playing
		play.playing = engine.playing
		writeStatus()
	}

	// MARK: plugin

	private func switchPlugin(to info: SDPluginInfo, saveOld: Bool = true) {
		if saveOld, loaded != nil { captureState() }
		if load(info, state: states[info.name]) {
			song.plugin = info.name
			scheduleSave()
			status("loaded \(info.shortName) \(info.version)", Theme.ok)
		}
	}

	// Loads a fresh copy of the plugin and swaps it in; the old one goes once the audio thread and
	// its view have let go of it.
	@discardableResult
	private func load(_ info: SDPluginInfo, state: Data?) -> Bool {
		var err: NSString?
		guard let p = SDLoadedPlugin.load(info, engine: engine, error: &err) else {
			status("\(info.name): \(err ?? "load failed")", Theme.red)
			list.message = "\(info.name): \(err ?? "load failed")"
			return false
		}
		if let state, !p.restoreState(state) { status("\(info.name): the saved state didn't load", Theme.warn) }
		let old = loaded
		engine.setUnit(p.unit, channels: p.channels, effect: info.isEffect, midi: info.acceptsMIDI)
		loaded = p
		loadedStamp = info.stamp
		pendingStamp = ""
		autoreleasepool {
			pluginView?.removeFromSuperview()
			pluginView = nil
		}
		if let v = p.makeView() {
			v.postsFrameChangedNotifications = true
			pluginSize = v.frame.size
			pluginView = v
		}
		list.message = nil
		if let old { DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { old.dispose() } }
		pluginPill.title = info.shortName
		applyMode()
		refreshShown()
		fitWindow()
		updateFooter()
		writeStatus()
		return true
	}

	private func unloadPlugin() {
		captureState()
		saveStates()
		engine.setUnit(nil, channels: 2, effect: false, midi: false)
		pluginView?.removeFromSuperview()
		pluginView = nil
		if let old = loaded { DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { old.dispose() } }
		loaded = nil
		loadedStamp = ""
		pluginPill.title = "no plugin"
		refreshShown()
		song.plugin = nil
		applyMode()
		scheduleSave()
		fitWindow()
		updateFooter()
		writeStatus()
	}

	private func reload(_ why: String) {
		guard let cur = loaded else { return }
		let fresh = pluginInfo(named: cur.info.name) ?? cur.info
		let state = cur.state()
		if !load(fresh, state: state) {
			loadedStamp = fresh.stamp // wait for the next build
		} else {
			reloads += 1
			let f = DateFormatter()
			f.dateFormat = "HH:mm:ss"
			status("\(why): \(fresh.shortName) \(fresh.version), built \(f.string(from: fresh.built))", Theme.ok)
		}
	}

	private func captureState() {
		if let l = loaded, let d = l.state() { states[l.info.name] = d }
		for (i, u) in fx.enumerated() {
			if let u, let d = u.plugin.state() { states[fxStateKey(i, u.info.name)] = d }
		}
	}

	// MARK: effects

	private func fxStateKey(_ i: Int, _ name: String) -> String { "fx\(i):\(name)" }

	private func loadEffect(_ info: SDPluginInfo, state: Data?) -> EffectUnit? {
		var err: NSString?
		guard let p = SDLoadedPlugin.load(info, engine: engine, error: &err) else {
			status("\(info.shortName): \(err ?? "load failed")", Theme.red)
			return nil
		}
		if let state, !p.restoreState(state) { status("\(info.shortName): the saved state didn't load", Theme.warn) }
		let v = p.makeView()
		v?.postsFrameChangedNotifications = true
		return EffectUnit(info: info, plugin: p, view: v)
	}

	// The engine's chain, from song.effects and the loaded units.
	private func pushChain() {
		var units: [AudioUnit?] = [], channels: [Int32] = [], bypass: [ObjCBool] = []
		for (i, e) in song.effects.enumerated() {
			guard i < fx.count, let u = fx[i] else { continue }
			units.append(u.plugin.unit)
			channels.append(Int32(u.plugin.channels))
			bypass.append(ObjCBool(e.bypass))
		}
		units.withUnsafeBufferPointer { ub in
			channels.withUnsafeBufferPointer { cb in
				bypass.withUnsafeBufferPointer { bb in
					engine.setEffects(ub.baseAddress, channels: cb.baseAddress, bypass: bb.baseAddress, count: units.count)
				}
			}
		}
	}

	private func disposeEffects(_ units: [EffectUnit]) {
		for u in units { u.view?.removeFromSuperview() }
		DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { units.forEach { $0.plugin.dispose() } }
	}

	// Makes the loaded effects match song.effects: an effect already loaded at the same place
	// stays; others load (with their saved state); the rest go.
	private func syncEffects() {
		var old = fx
		var next: [EffectUnit?] = []
		for (i, e) in song.effects.enumerated() {
			if i < old.count, let u = old[i], u.info.name == e.plugin {
				next.append(u)
				old[i] = nil
			} else if let info = pluginInfo(named: e.plugin) {
				next.append(loadEffect(info, state: states[fxStateKey(i, info.name)]))
			} else {
				status("effect \(e.plugin) not found", Theme.warn)
				next.append(nil)
			}
		}
		fx = next
		pushChain()
		disposeEffects(old.compactMap { $0 })
		refreshShown()
		fitWindow()
	}

	private func addEffect(_ info: SDPluginInfo) {
		guard song.effects.count < 8 else { status("8 effects at most", Theme.warn); return }
		guard let u = loadEffect(info, state: nil) else { return }
		song.effects.append(EffectSlot(plugin: info.name))
		fx.append(u)
		pushChain()
		shown = fx.count
		refreshShown()
		fitWindow()
		scheduleSave()
		status("added \(info.shortName)", Theme.ok)
	}

	private func removeEffect(_ i: Int) {
		guard i < song.effects.count else { return }
		let name = song.effects[i].plugin
		song.effects.remove(at: i)
		let u = i < fx.count ? fx.remove(at: i) : nil
		pushChain()
		if let u { disposeEffects([u]) }
		if shown == i + 1 { shown = i } else if shown > i + 1 { shown -= 1 }
		captureState() // the later effects' states under their new places
		refreshShown()
		fitWindow()
		scheduleSave()
		status("removed \(name)", Theme.inkSoft)
	}

	private func toggleBypass(_ i: Int) {
		guard i < song.effects.count else { return }
		song.effects[i].bypass.toggle()
		pushChain()
		updateTabs()
		scheduleSave()
	}

	private func reloadEffect(_ i: Int, _ info: SDPluginInfo) {
		guard i < fx.count, let old = fx[i] else { return }
		guard let u = loadEffect(info, state: old.plugin.state()) else {
			old.stamp = info.stamp // wait for the next build
			return
		}
		fx[i] = u
		pushChain()
		disposeEffects([old])
		refreshShown()
		fitWindow()
		reloads += 1
		updateFooter()
		writeStatus()
		let f = DateFormatter()
		f.dateFormat = "HH:mm:ss"
		status("rebuilt, reloaded: \(info.shortName) \(info.version), built \(f.string(from: info.built))", Theme.ok)
	}

	private func effectMenu() -> NSMenu {
		let m = NSMenu()
		let effects = plugins.filter(\.isEffect)
		if effects.isEmpty { m.addItem(withTitle: "No effects found", action: nil, keyEquivalent: "") }
		for p in effects { m.addItem(item("\(p.name)   \(p.version)") { [weak self] in self?.addEffect(p) }) }
		return m
	}

	private func saveStates() {
		captureState()
		guard !states.isEmpty, let d = try? PropertyListSerialization.data(fromPropertyList: states, format: .binary, options: 0) else { return }
		try? d.write(to: statesURL, options: .atomic)
	}

	// MARK: watching

	private func tick() {
		ticks += 1
		// The plugins: new ones listed; the loaded one reloaded once its new build has stopped
		// changing for one scan.
		let found = SDPluginInfo.scan()
		if found.map({ "\($0.name) \($0.stamp)" }) != plugins.map({ "\($0.name) \($0.stamp)" }) {
			plugins = found
			list.plugins = found
		}
		if let cur = loaded, let fresh = pluginInfo(named: cur.info.name), fresh.stamp != loadedStamp {
			if fresh.stamp == pendingStamp { reload("rebuilt, reloaded") } else { pendingStamp = fresh.stamp }
		}
		for (i, u) in fx.enumerated() {
			guard let u, let fresh = pluginInfo(named: u.info.name), fresh.stamp != u.stamp else { continue }
			if fresh.stamp == u.pending { reloadEffect(i, fresh) } else { u.pending = fresh.stamp }
		}
		if let m = mtime(songURL), m != songMTime, saveWork == nil {
			songMTime = m
			songChangedOnDisk()
		}
		if ticks % 40 == 0 { saveStates() }
		if ticks % 4 == 0 { updateFooter() }

	}

	private var loadText = ""
	private var lastLoad = Date()
	private func frame() {
		roll.playBeat = engine.positionBeats()
		drums.playBeat = roll.playBeat
		var l: Float = 0, r: Float = 0
		engine.peaks(&l, right: &r)
		meter.set(l, r)
		if Date().timeIntervalSince(lastLoad) > 0.5 {
			lastLoad = Date()
			var avg = 0.0, peak = 0.0
			engine.load(&avg, peak: &peak)
			meter.load = loaded == nil ? "" : String(format: "DSP %4.1f%%  peak %4.1f%%", avg * 100, peak * 100)
		}
		if play.playing != engine.playing { play.playing = engine.playing }
	}

	private func mtime(_ url: URL) -> Date? {
		(try? FileManager.default.attributesOfItem(atPath: url.path))?[.modificationDate] as? Date
	}

	private func status(_ text: String, _ colour: NSColor = Theme.inkSoft) {
		statusLabel.stringValue = text
		statusLabel.textColor = colour
		layout()
		statusWork?.cancel()
		let w = DispatchWorkItem { [weak self] in self?.statusLabel.stringValue = ""; self?.layout() }
		statusWork = w
		DispatchQueue.main.asyncAfter(deadline: .now() + 10, execute: w)
		NSLog("Simple DAW: %@", text)
	}

	private func updateFooter() {
		let home = FileManager.default.homeDirectoryForCurrentUser.path
		var parts = ["song " + songURL.path.replacingOccurrences(of: home, with: "~")]
		if let l = loaded {
			let f = DateFormatter()
			f.dateFormat = "d MMM HH:mm:ss"
			parts.append("\(l.info.shortName) \(l.info.version), built \(f.string(from: l.info.built))" + (reloads > 0 ? ", \(reloads) reload\(reloads == 1 ? "" : "s")" : ""))
		}
		parts.append(String(format: "%.0f Hz / %u", engine.sampleRate, engine.bufferFrames))
		parts.append("\(engine.midiSources) MIDI in")
		footerText.stringValue = parts.joined(separator: "   ·   ")
	}

	// What's open, for tools outside (Claude): ~/Library/Application Support/Simple DAW/status.json
	private func writeStatus(running: Bool = true) {
		var s: [String: Any] = ["running": running, "song": songURL.path, "playing": engine.playing, "bpm": song.bpm,
		                        "bars": song.bars, "pid": ProcessInfo.processInfo.processIdentifier, "reloads": reloads]
		s["effects"] = song.effects.map { ["plugin": $0.plugin, "bypass": $0.bypass] }
		if let l = loaded {
			s["plugin"] = l.info.name
			s["pluginPath"] = l.info.bundlePath
			s["version"] = l.info.version
			s["built"] = ISO8601DateFormatter().string(from: l.info.built)
		}
		if let d = try? JSONSerialization.data(withJSONObject: s, options: [.prettyPrinted, .sortedKeys]) {
			try? d.write(to: supportDir.appendingPathComponent("status.json"), options: .atomic)
		}
	}


	// MARK: menus

	private func item(_ title: String, _ key: String = "", _ mods: NSEvent.ModifierFlags = [.command], _ act: @escaping () -> Void) -> NSMenuItem {
		let i = ClosureItem(title: title, key: key, act: act)
		i.keyEquivalentModifierMask = mods
		return i
	}

	private func pluginMenu() -> NSMenu {
		let m = NSMenu()
		let instruments = plugins.filter { !$0.isEffect }, effects = plugins.filter(\.isEffect)
		for (head, group) in [("Instruments", instruments), ("Effects", effects)] where !group.isEmpty {
			m.addItem(NSMenuItem.sectionHeader(title: head))
			for p in group {
				let i = item("\(p.name)   \(p.version)") { [weak self] in self?.switchPlugin(to: p) }
				i.state = loaded?.info.name == p.name ? .on : .off
				m.addItem(i)
			}
		}
		m.addItem(.separator())
		m.addItem(item("None") { [weak self] in self?.unloadPlugin() })
		m.addItem(item("Reload Now", "r") { [weak self] in self?.reload("reloaded") })
		m.addItem(item("Open Plugin File…") { [weak self] in self?.openPluginFile() })
		return m
	}

	private func barsMenu() -> NSMenu {
		let m = NSMenu()
		for n in [1, 2, 4, 8, 16] {
			let i = item("\(n) bar" + (n == 1 ? "" : "s")) { [weak self] in self?.setBars(n) }
			i.state = song.bars == n ? .on : .off
			m.addItem(i)
		}
		return m
	}

	private func gridMenu() -> NSMenu {
		let m = NSMenu()
		for g in [1, 0.5, 0.25, 0.125, 1.0 / 3, 1.0 / 6] {
			let i = item(gridName(g)) { [weak self] in self?.setGrid(g) }
			i.state = abs(song.grid - g) < 1e-6 ? .on : .off
			m.addItem(i)
		}
		return m
	}

	private func openPluginFile() {
		let p = NSOpenPanel()
		p.allowedFileTypes = ["component"]
		p.treatsFilePackagesAsDirectories = false
		if p.runModal() == .OK, let u = p.url, let info = SDPluginInfo.infos(forBundle: u.path, named: u.path).first { switchPlugin(to: info) }
	}

	private func buildMenu() {
		let main = NSMenu()
		let appMenu = NSMenu()
		appMenu.addItem(withTitle: "About Simple DAW", action: #selector(NSApplication.orderFrontStandardAboutPanel(_:)), keyEquivalent: "")
		appMenu.addItem(.separator())
		appMenu.addItem(withTitle: "Hide Simple DAW", action: #selector(NSApplication.hide(_:)), keyEquivalent: "h")
		appMenu.addItem(withTitle: "Quit Simple DAW", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
		let file = NSMenu(title: "File")
		file.addItem(item("Open Song…", "o") { [weak self] in self?.openSongPanel() })
		file.addItem(item("Save Song As…", "s", [.command, .shift]) { [weak self] in self?.saveSongAs() })
		file.addItem(item("Reveal Song in Finder") { [weak self] in
			guard let self else { return }
			NSWorkspace.shared.activateFileViewerSelecting([self.songURL])
		})
		file.addItem(.separator())
		file.addItem(item("Clear Sequence") { [weak self] in self?.roll.apply([], name: "Clear") })
		file.addItem(item("Drum Mode", "d") { [weak self] in self?.toggleMode() })
		let edit = NSMenu(title: "Edit")
		edit.addItem(withTitle: "Undo", action: Selector(("undo:")), keyEquivalent: "z")
		let redo = edit.addItem(withTitle: "Redo", action: Selector(("redo:")), keyEquivalent: "z")
		redo.keyEquivalentModifierMask = [.command, .shift]
		edit.addItem(.separator())
		edit.addItem(withTitle: "Cut", action: #selector(NSText.cut(_:)), keyEquivalent: "x")
		edit.addItem(withTitle: "Copy", action: #selector(NSText.copy(_:)), keyEquivalent: "c")
		edit.addItem(withTitle: "Paste", action: #selector(NSText.paste(_:)), keyEquivalent: "v")
		edit.addItem(withTitle: "Select All", action: #selector(NSText.selectAll(_:)), keyEquivalent: "a")
		let transport = NSMenu(title: "Transport")
		transport.addItem(item("Play / Stop (Space)") { [weak self] in self?.togglePlay() })
		let plugin = NSMenu(title: "Plugin")
		plugin.addItem(item("Reload Now", "r") { [weak self] in self?.reload("reloaded") })
		plugin.addItem(item("Open Plugin File…") { [weak self] in self?.openPluginFile() })
		plugin.addItem(item("Show Components Folder") {
			NSWorkspace.shared.open(FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Audio/Plug-Ins/Components"))
		})
		let windowMenu = NSMenu(title: "Window")
		windowMenu.addItem(withTitle: "Minimize", action: #selector(NSWindow.performMiniaturize(_:)), keyEquivalent: "m")
		for (title, sub) in [("Simple DAW", appMenu), ("File", file), ("Edit", edit), ("Transport", transport), ("Plugin", plugin), ("Window", windowMenu)] {
			let top = NSMenuItem(title: title, action: nil, keyEquivalent: "")
			top.submenu = sub
			main.addItem(top)
		}
		NSApp.mainMenu = main
		NSApp.windowsMenu = windowMenu
	}

	private func openSongPanel() {
		let p = NSOpenPanel()
		p.allowedFileTypes = ["json"]
		p.directoryURL = songURL.deletingLastPathComponent()
		if p.runModal() == .OK, let u = p.url { openSong(u) }
	}

	private func saveSongAs() {
		let p = NSSavePanel()
		p.allowedFileTypes = ["json"]
		p.nameFieldStringValue = songURL.lastPathComponent
		p.directoryURL = songURL.deletingLastPathComponent()
		guard p.runModal() == .OK, let u = p.url else { return }
		saveStates()
		songURL = u
		saveSong()
		saveStates()
		UserDefaults.standard.set(u.path, forKey: "song")
		window.title = "Simple DAW — \(u.lastPathComponent)"
		updateFooter()
	}
}

extension SDPluginInfo {
	// The bundle's name, also for a plugin opened from a path.
	var shortName: String { name.hasPrefix("/") ? URL(fileURLWithPath: name).deletingPathExtension().lastPathComponent : name }
}

final class ClosureItem: NSMenuItem {
	private let act: () -> Void
	init(title: String, key: String, act: @escaping () -> Void) {
		self.act = act
		super.init(title: title, action: #selector(run), keyEquivalent: key)
		target = self
	}
	required init(coder: NSCoder) { fatalError() }
	@objc private func run() { act() }
}
