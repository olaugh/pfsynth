import SwiftUI
import UniformTypeIdentifiers

struct ContentView: View {
    @EnvironmentObject var m: DemoModel
    @State private var settingsQuery = ""
    @State private var dropTargeted = false
    @FocusState private var settingsFocused: Bool
    @FocusState private var searchFocused: Bool
    var body: some View {
        NavigationSplitView {
            VStack(alignment: .leading, spacing: 8) {
                HStack(spacing: 6) {
                    Image(systemName: "magnifyingglass").foregroundStyle(.secondary)
                    TextField("Search pieces by composer or title", text: $m.searchText).textFieldStyle(.plain).focused($searchFocused)
                    if !m.searchText.isEmpty { Button { m.searchText = "" } label: { Image(systemName: "xmark.circle.fill").foregroundStyle(.secondary) }.buttonStyle(.plain) }
                }.padding(6).background(RoundedRectangle(cornerRadius: 6).fill(Color.primary.opacity(0.06))).padding(.horizontal, 10).padding(.top, 8)
                Picker("Challenge", selection: $m.tagFilter) {
                    Text("All").tag(String?.none)
                    ForEach(Library.tags, id: \.self) { Text($0.capitalized).tag(String?.some($0)) }
                }.pickerStyle(.menu).padding(.horizontal, 10)
                List(m.filtered, selection: Binding(get: { m.selection }, set: { new in
                    // Only record the selection here; loading publishes many changes and must not
                    // happen inside the list's selection callback (it re-enters until the stack overflows).
                    guard let p = new, p != m.selection else { return }
                    m.selection = p
                    Task { @MainActor in m.select(p) }
                })) { p in
                    VStack(alignment: .leading, spacing: 3) {
                        Text(p.title).font(.headline).lineLimit(2)
                        Text("\(p.composer) · \(Int(p.stats.duration / 60)):\(String(format: "%02d", Int(p.stats.duration) % 60)) · \(p.corpus)").font(.caption).foregroundStyle(.secondary)
                        HStack(spacing: 4) { ForEach(p.tags, id: \.self) { Text($0).font(.caption2).padding(.horizontal, 6).padding(.vertical, 1).background(Capsule().fill(color(for: $0).opacity(0.25))) } }
                    }.padding(.vertical, 2).opacity(p.exists ? 1 : 0.4).tag(p)
                }.listStyle(.sidebar)
                HStack { Button { m.openFile() } label: { Label("Open MIDI…", systemImage: "folder") }; Text("or drop files on the window").font(.caption).foregroundStyle(.secondary) }.padding(10)
            }.navigationSplitViewColumnWidth(min: 260, ideal: 300)
        } detail: {
            VStack(spacing: 10) {
                header
                PianoRollView(scene: m.scene).clipShape(RoundedRectangle(cornerRadius: 8)).frame(minHeight: 320)
                transport
                HStack(alignment: .top, spacing: 24) { optionsPanel; Divider(); exportPanel }.padding(.bottom, 6)
            }.padding(14)
            .onDrop(of: [.fileURL], isTargeted: $dropTargeted) { providers in
                Task { @MainActor in
                    var urls: [URL] = []
                    for p in providers { if let u = try? await p.loadItem(forTypeIdentifier: UTType.fileURL.identifier) as? Data, let url = URL(dataRepresentation: u, relativeTo: nil) { urls.append(url) } else if let u = try? await p.loadItem(forTypeIdentifier: UTType.fileURL.identifier) as? URL { urls.append(u) } }
                    m.open(urls: urls)
                }
                return true
            }
            .overlay { if dropTargeted { RoundedRectangle(cornerRadius: 10).strokeBorder(Color.accentColor, style: StrokeStyle(lineWidth: 3, dash: [8])).padding(6).overlay(Text("Drop MIDI files to play them").font(.title3).padding(10).background(.thinMaterial, in: RoundedRectangle(cornerRadius: 8))).allowsHitTesting(false) } }
        }
        .background { Button("") { settingsFocused = true }.keyboardShortcut("k", modifiers: .command).hidden(); Button("") { searchFocused = true }.keyboardShortcut("f", modifiers: .command).hidden() }
    }
    var header: some View {
        VStack(alignment: .leading, spacing: 4) {
            if let p = m.selection {
                HStack(alignment: .firstTextBaseline) { Text(p.title).font(.title2.bold()); Text(p.composer).font(.title3).foregroundStyle(.secondary); Spacer(); Text(p.license).font(.caption).foregroundStyle(.tertiary) }
                Text(p.why).font(.callout).foregroundStyle(.secondary)
                HStack(spacing: 14) {
                    stat("max polyphony", "\(p.stats.max_polyphony)"); stat("peak notes / 2 s", "\(p.stats.peak_notes_2s)"); stat("velocity", "\(p.stats.vel_p5)–\(p.stats.vel_p95)")
                    stat("repeated notes", String(format: "%.0f%%", p.stats.repeated_rate * 100)); stat("above F6", String(format: "%.0f%%", (p.stats.high_share ?? 0) * 100)); stat("at/below E2", String(format: "%.0f%%", (p.stats.low_share ?? 0) * 100)); stat("half pedal", String(format: "%.0f%%", p.pedal.half_share * 100)); stat("soft-pedal moves", "\(p.pedal.cc67)"); if p.pedal.cc66 > 0 { stat("sostenuto", "\(p.pedal.cc66)") }
                    Spacer(); stat("voices", "\(m.activeVoices)"); loadMeter
                }.font(.caption)
            } else { Text("pfsynth piano demo").font(.title2.bold()); Text("Pick a piece on the left. Space plays, ⌘. stops.").foregroundStyle(.secondary) }
        }
    }
    func stat(_ k: String, _ v: String) -> some View { HStack(spacing: 4) { Text(k).foregroundStyle(.secondary); Text(v).monospacedDigit().bold() } }
    /// Real-time headroom: render time as a share of the audio callback's budget. Above 100% the buffer underruns (crackle).
    var loadMeter: some View {
        let load = m.dspLoad, peak = m.dspPeak
        let color: Color = peak > 0.85 ? .red : peak > 0.6 ? .orange : .green
        return HStack(spacing: 6) {
            Text("DSP load").foregroundStyle(.secondary)
            ZStack(alignment: .leading) {
                RoundedRectangle(cornerRadius: 2).fill(Color.secondary.opacity(0.2)).frame(width: 90, height: 8)
                RoundedRectangle(cornerRadius: 2).fill(color).frame(width: 90 * min(load, 1), height: 8)
                Rectangle().fill(Color.primary.opacity(0.7)).frame(width: 1.5, height: 10).offset(x: 90 * min(peak, 1))
            }
            Text(String(format: "%.0f%% · peak %.0f%%", load * 100, peak * 100)).monospacedDigit().foregroundStyle(color)
        }.help("Share of each audio buffer's time spent rendering; at 100% playback drops out")
    }
    var transport: some View {
        VStack(spacing: 6) {
            HStack(spacing: 12) {
                Button { m.togglePlay() } label: { Image(systemName: m.isPlaying ? "pause.fill" : "play.fill").frame(width: 22) }.keyboardShortcut(" ", modifiers: []).disabled(m.selection == nil)
                Button { m.stop() } label: { Image(systemName: "stop.fill") }.disabled(m.selection == nil)
                Text(m.fmt(m.time)).monospacedDigit().frame(width: 70, alignment: .leading)
                Slider(value: Binding(get: { m.time }, set: { m.seek($0) }), in: 0...max(m.song.duration, 1)).disabled(m.selection == nil)
                Text(m.fmt(m.song.duration)).monospacedDigit().foregroundStyle(.secondary)
            }
            HStack(spacing: 10) {
                Text("Excerpt").foregroundStyle(.secondary)
                Text("in \(m.fmt(m.excerptIn))").monospacedDigit(); Button("Set in") { m.setIn() }.disabled(m.selection == nil)
                Text("out \(m.fmt(m.excerptOut))").monospacedDigit(); Button("Set out") { m.setOut() }.disabled(m.selection == nil)
                Button("Whole piece") { m.wholePiece() }.disabled(m.selection == nil)
                if m.selection?.excerpt != nil { Button("Suggested excerpt") { m.suggestedExcerpt() } }
                Spacer()
                Text(m.status).font(.caption).foregroundStyle(.secondary).lineLimit(1)
            }.font(.callout)
        }
    }
    // MARK: settings palette — every control with its known-good default; the search field filters rows across sections
    func show(_ keywords: String) -> Bool {
        let words = settingsQuery.lowercased().split(separator: " ").map(String.init)
        return words.allSatisfy { keywords.lowercased().contains($0) }
    }
    var searching: Bool { !settingsQuery.trimmingCharacters(in: .whitespaces).isEmpty }
    var optionsPanel: some View {
        let d = SynthOptions()
        return VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 8) {
                Text("Settings").font(.headline)
                HStack(spacing: 4) {
                    Image(systemName: "magnifyingglass").foregroundStyle(.secondary)
                    TextField("Search settings (⌘K)", text: $settingsQuery).textFieldStyle(.plain).focused($settingsFocused)
                    if searching { Button { settingsQuery = "" } label: { Image(systemName: "xmark.circle.fill").foregroundStyle(.secondary) }.buttonStyle(.plain) }
                }.padding(4).background(RoundedRectangle(cornerRadius: 6).fill(Color.primary.opacity(0.06))).frame(width: 220)
                Button("Reset all") { m.options = SynthOptions() }.disabled(m.options == d).font(.callout)
            }
            ScrollView {
                VStack(alignment: .leading, spacing: 6) {
                    section("Sound", "sound gain resonance sympathetic") {
                        if show("gain volume level sound") { slider("Gain", $m.options.gainDb, d.gainDb, -6...18, "%+.0f dB", "live only; the limiter prevents clipping and exports are normalized") }
                        if show("sympathetic resonance strings pedal halo sound") { toggle("Sympathetic resonance", $m.options.resonance, d.resonance, "undamped strings ring in sympathy: pedal down, held keys, the top 1.5 octaves (prototype, fitted to Pianoteq)") }
                        if show("sympathetic resonance level halo sound") { slider("Resonance level", $m.options.resonanceDb, d.resonanceDb, -24...12, "%+.0f dB", "0 dB = as fitted: other strings' partials 20–28 dB under the note").disabled(!m.options.resonance) }
                    }
                    section("Onset", "onset attack thump body knock noise soundboard") {
                        if show("onset layer attack soundboard thump noise") { toggle("Onset layer", $m.options.attack, d.attack, "soundboard thump, knock modes and hammer noise at every note-on") }
                        Group {
                            if show("body slow modes soundboard thump onset") { slider("Body (slow modes)", $m.options.bodyDb, d.bodyDb, -24...6, "%+.0f dB", "low body/room modes 59–450 Hz; ear-chosen −18") }
                            if show("knock fast modes click onset") { slider("Knock (fast modes)", $m.options.knockDb, d.knockDb, -24...6, "%+.0f dB", "fast soundboard modes to 2.8 kHz: the percussive click") }
                            if show("noise burst hammer hiss onset") { slider("Noise burst", $m.options.noiseDb, d.noiseDb, -24...6, "%+.0f dB", "filtered noise between the partials in the first tens of ms") }
                        }.disabled(!m.options.attack)
                    }
                    section("Pedals", "pedal sustain damper half una corda soft sostenuto") {
                        if show("continuous damper half pedaling sustain pedal cc64") { toggle("Continuous damper (half pedaling)", $m.options.continuousPedal, d.continuousPedal, "the damper follows the raw CC64 value; off = binary sustain with a fixed release (legacy)") }
                        if show("una corda soft pedal cc67") { toggle("Una corda from CC67", $m.options.unaCorda, d.unaCorda, "−2.1 dB on the fundamental, +1.7 dB/octave of partial index, slower fundamental decay").disabled(!m.options.continuousPedal) }
                    }
                    section("Experimental", "experimental resonance coupling skirt sustain tilt decay", collapsed: true, hint: "Sympathetic-resonance internals (fitted to lone notes, pedal down vs up). Changing one rebuilds the bank.") {
                        if show("resonance coupling experimental") { slider("Resonance coupling", $m.options.resCoupling, d.resCoupling, -50...(-10), "%+.0f dB", "free amplitude of a coincident string partial re the note's partial") }
                        if show("resonance skirt width hz experimental") { slider("Resonance skirt", $m.options.resSkirt, d.resSkirt, 0.5...16, "%.1f Hz", "how far from a played partial a string still gets excited") }
                        if show("resonance sustain bound driven experimental") { slider("Resonance sustain bound", $m.options.resSustain, d.resSustain, -40...0, "%+.0f dB", "cap on the driven build-up of a string sitting on a played partial") }
                        if show("resonance tilt octave experimental") { slider("Resonance tilt", $m.options.resTilt, d.resTilt, -12...6, "%+.1f dB/oct", "coupling change per octave above 250 Hz") }
                        if show("resonance decay scale t60 experimental") { slider("Resonance decay scale", $m.options.resT60, d.resT60, 0.25...4, "%.2f ×", "multiplies the sympathetic strings' free decay times") }
                    }
                    section("Legacy", "legacy salamander tone binary pedal", collapsed: true, hint: "Earlier behaviour kept for A/B listening; not the known-good path.") {
                        if show("salamander tone patch legacy pianoteq") { toggle("Salamander-fitted tone", Binding(get: { !m.options.pianoteqTone }, set: { m.options.pianoteqTone = !$0 }), !d.pianoteqTone, "the frozen 2026-09-04 baseline patch instead of the Pianoteq-fitted one") }
                        if show("binary sustain pedal legacy release") { toggle("Binary sustain pedal", Binding(get: { !m.options.continuousPedal }, set: { m.options.continuousPedal = !$0 }), !d.continuousPedal, "CC64 ≥ 64 holds, else a fixed 240 ms release") }
                    }
                }.padding(.trailing, 6)
            }.frame(maxHeight: 250)
            Text("Tone and onset changes apply to the next notes; pedal and resonance changes apply immediately.").font(.caption).foregroundStyle(.tertiary)
        }.frame(minWidth: 520)
    }
    @ViewBuilder func section<Content: View>(_ title: String, _ keywords: String, collapsed: Bool = false, hint: String? = nil, @ViewBuilder content: () -> Content) -> some View {
        let _ = keywords
        let body = content()
        DisclosureGroup(isExpanded: Binding(get: { searching || (collapsed ? expanded.contains(title) : !expanded.contains(title)) }, set: { v in if v == collapsed { expanded.insert(title) } else { expanded.remove(title) } })) {
            VStack(alignment: .leading, spacing: 4) { if let h = hint { Text(h).font(.caption).foregroundStyle(.secondary) }; body }.padding(.leading, 4).padding(.top, 2)
        } label: { Text(title).font(.subheadline.bold()).foregroundStyle(collapsed ? .secondary : .primary) }
    }
    @State private var expanded: Set<String> = []
    func slider(_ label: String, _ value: Binding<Double>, _ def: Double, _ range: ClosedRange<Double>, _ fmt: String, _ help: String) -> some View {
        HStack(spacing: 8) {
            Text(label).frame(width: 190, alignment: .leading)
            Slider(value: value, in: range).frame(width: 150)
            Text(String(format: fmt, value.wrappedValue)).monospacedDigit().frame(width: 70, alignment: .trailing)
            if abs(value.wrappedValue - def) > 1e-9 { Button(String(format: "default " + fmt, def)) { value.wrappedValue = def }.buttonStyle(.plain).foregroundStyle(.orange).font(.caption) } else { Text("default").font(.caption).foregroundStyle(.tertiary) }
        }.font(.callout).help(help)
    }
    func toggle(_ label: String, _ value: Binding<Bool>, _ def: Bool, _ help: String) -> some View {
        HStack(spacing: 8) {
            Toggle(label, isOn: value).frame(width: 340, alignment: .leading)
            if value.wrappedValue != def { Button("default \(def ? "on" : "off")") { value.wrappedValue = def }.buttonStyle(.plain).foregroundStyle(.orange).font(.caption) } else { Text("default").font(.caption).foregroundStyle(.tertiary) }
        }.font(.callout).help(help)
    }
    var exportPanel: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Export excerpt").font(.headline)
            HStack { Button("MP3…") { m.export(.mp3) }.disabled(Exporter.mp3Encoder == nil); Button("M4A…") { m.export(.m4a) }; Button("MP4 with piano roll…") { m.export(.mp4) } }.disabled(m.selection == nil || m.exporting)
            if Exporter.mp3Encoder == nil { Text("MP3 needs lame or ffmpeg (brew install lame).").font(.caption).foregroundStyle(.secondary) }
            if m.exporting { HStack { ProgressView(value: m.exportProgress).frame(width: 220); Button("Cancel") { m.cancel() } } }
            Text("Audio is peak-normalized to −1 dBFS. Video: 1280×720, 30 fps, H.264 + AAC.").font(.caption).foregroundStyle(.tertiary)
        }
    }
    func color(for tag: String) -> Color { switch tag { case "polyphony": return .purple; case "pedalling": return .blue; case "dynamics": return .orange; case "repeated notes": return .green; case "high register": return .pink; case "low register": return .brown; default: return .gray } }
}
