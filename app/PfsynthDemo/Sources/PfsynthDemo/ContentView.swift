import SwiftUI

struct ContentView: View {
    @EnvironmentObject var m: DemoModel
    var body: some View {
        NavigationSplitView {
            VStack(alignment: .leading, spacing: 8) {
                Picker("Challenge", selection: $m.tagFilter) {
                    Text("All").tag(String?.none)
                    ForEach(Library.tags, id: \.self) { Text($0.capitalized).tag(String?.some($0)) }
                }.pickerStyle(.menu).padding(.horizontal, 10).padding(.top, 8)
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
                Button { m.openFile() } label: { Label("Open MIDI…", systemImage: "folder") }.padding(10)
            }.navigationSplitViewColumnWidth(min: 260, ideal: 300)
        } detail: {
            VStack(spacing: 10) {
                header
                PianoRollView(scene: m.scene).clipShape(RoundedRectangle(cornerRadius: 8)).frame(minHeight: 320)
                transport
                HStack(alignment: .top, spacing: 24) { optionsPanel; Divider(); exportPanel }.padding(.bottom, 6)
            }.padding(14)
        }
    }
    var header: some View {
        VStack(alignment: .leading, spacing: 4) {
            if let p = m.selection {
                HStack(alignment: .firstTextBaseline) { Text(p.title).font(.title2.bold()); Text(p.composer).font(.title3).foregroundStyle(.secondary); Spacer(); Text(p.license).font(.caption).foregroundStyle(.tertiary) }
                Text(p.why).font(.callout).foregroundStyle(.secondary)
                HStack(spacing: 14) {
                    stat("max polyphony", "\(p.stats.max_polyphony)"); stat("peak notes / 2 s", "\(p.stats.peak_notes_2s)"); stat("velocity", "\(p.stats.vel_p5)–\(p.stats.vel_p95)")
                    stat("repeated notes", String(format: "%.0f%%", p.stats.repeated_rate * 100)); stat("above F6", String(format: "%.0f%%", (p.stats.high_share ?? 0) * 100)); stat("half pedal", String(format: "%.0f%%", p.pedal.half_share * 100)); stat("soft-pedal moves", "\(p.pedal.cc67)"); if p.pedal.cc66 > 0 { stat("sostenuto", "\(p.pedal.cc66)") }
                    Spacer(); stat("voices", "\(m.activeVoices)")
                }.font(.caption)
            } else { Text("pfsynth piano demo").font(.title2.bold()); Text("Pick a piece on the left. Space plays, ⌘. stops.").foregroundStyle(.secondary) }
        }
    }
    func stat(_ k: String, _ v: String) -> some View { HStack(spacing: 4) { Text(k).foregroundStyle(.secondary); Text(v).monospacedDigit().bold() } }
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
    var optionsPanel: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Model").font(.headline)
            Picker("Tone", selection: $m.options.pianoteqTone) { Text("Salamander-fitted").tag(false); Text("Pianoteq-fitted").tag(true) }.pickerStyle(.segmented).frame(width: 300)
            Toggle("Onset (soundboard thump + noise)", isOn: $m.options.attack)
            Picker("Sustain pedal", selection: $m.options.continuousPedal) { Text("Binary").tag(false); Text("Continuous damper").tag(true) }.pickerStyle(.segmented).frame(width: 300)
            Toggle("Una corda from CC67", isOn: $m.options.unaCorda).disabled(!m.options.continuousPedal)
            HStack { Text("Gain"); Slider(value: $m.options.gainDb, in: -6...24).frame(width: 180); Text(String(format: "%+.0f dB", m.options.gainDb)).monospacedDigit() }
            Text("Changes to tone and onset apply to the next notes; pedal changes apply immediately.").font(.caption).foregroundStyle(.tertiary)
        }
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
    func color(for tag: String) -> Color { switch tag { case "polyphony": return .purple; case "pedalling": return .blue; case "dynamics": return .orange; case "repeated notes": return .green; case "high register": return .pink; default: return .gray } }
}
