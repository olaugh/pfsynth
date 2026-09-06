import SwiftUI
import PfsynthCore

/// Headless use:  PfsynthDemo --export <in.mid> <out.mp3|m4a|mp4> [start end] [--salamander] [--no-attack] [--binary-pedal] [--no-soft] [--gain dB] [--body dB] [--knock dB] [--noise dB]
func runCommandLineIfRequested() {
    let a = CommandLine.arguments
    guard let i = a.firstIndex(of: "--export"), a.count > i + 2 else { return }
    setvbuf(stdout, nil, _IOLBF, 0)
    let midi = URL(fileURLWithPath: a[i + 1]), out = URL(fileURLWithPath: a[i + 2])
    var rest = Array(a[(i + 3)...]); var start = 0.0, end = Double.infinity; var o = SynthOptions()
    if let s = rest.first, let v = Double(s) { start = v; rest.removeFirst() }
    if let s = rest.first, let v = Double(s) { end = v; rest.removeFirst() }
    var it = rest.makeIterator()
    while let f = it.next() {
        switch f { case "--salamander": o.pianoteqTone = false; case "--no-attack": o.attack = false; case "--binary-pedal": o.continuousPedal = false; case "--no-soft": o.unaCorda = false
        case "--gain": if let g = it.next(), let v = Double(g) { o.gainDb = v }
        case "--body": if let g = it.next(), let v = Double(g) { o.bodyDb = v }
        case "--knock": if let g = it.next(), let v = Double(g) { o.knockDb = v }
        case "--noise": if let g = it.next(), let v = Double(g) { o.noiseDb = v }; default: break }
    }
    guard let fmt = ExportFormat.allCases.first(where: { $0.ext == out.pathExtension.lowercased() }) else { FileHandle.standardError.write("unknown output type \(out.pathExtension)\n".data(using: .utf8)!); exit(2) }
    do {
        var s = pf_song(); guard pf_midi_load(&s, midi.path) == 0 else { exit(1) }
        let song = digest(s); pf_midi_free(&s)
        let req = ExportRequest(midi: midi, output: out, start: start, end: min(end, song.duration), options: o, format: fmt, song: song, title: midi.deletingPathExtension().lastPathComponent)
        var last = -1
        try Exporter.run(req) { p, msg in let pct = Int(p * 100); if pct / 10 != last / 10 { print("\(msg) \(pct)%"); last = pct }; return true }
        print("wrote \(out.path)"); exit(0)
    } catch { FileHandle.standardError.write("export failed: \(error.localizedDescription)\n".data(using: .utf8)!); exit(1) }
}

@main
struct PfsynthDemoApp: App {
    @StateObject private var model = DemoModel()
    init() { runCommandLineIfRequested() }
    var body: some Scene {
        WindowGroup("pfsynth · piano demo") {
            ContentView().environmentObject(model).frame(minWidth: 1100, minHeight: 700)
        }
        .commands {
            CommandGroup(replacing: .newItem) {
                Button("Open MIDI…") { model.openFile() }.keyboardShortcut("o")
            }
            CommandMenu("Transport") {
                Button(model.isPlaying ? "Pause" : "Play") { model.togglePlay() }.keyboardShortcut(" ", modifiers: [])
                Button("Stop") { model.stop() }.keyboardShortcut(".", modifiers: .command)
            }
        }
    }
}
