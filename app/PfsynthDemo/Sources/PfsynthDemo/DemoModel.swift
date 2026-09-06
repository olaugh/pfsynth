import SwiftUI
import AVFoundation
import AppKit

@MainActor
final class DemoModel: ObservableObject {
    @Published var pieces: [Piece] = Library.all
    @Published var selection: Piece?
    @Published var song: SongData = .empty
    @Published var options = SynthOptions() { didSet { synth.setOptions(options) } }
    @Published var isPlaying = false
    @Published var time: Double = 0
    @Published var sounding: [Bool] = Array(repeating: false, count: 128)
    @Published var excerptIn: Double = 0
    @Published var excerptOut: Double = 0
    @Published var status = "Select a piece"
    @Published var exporting = false
    @Published var exportProgress = 0.0
    @Published var tagFilter: String? = nil
    @Published var searchText = ""
    @Published var activeVoices = 0
    @Published var dspLoad = 0.0
    @Published var dspPeak = 0.0

    let synth = Synth()
    private let engine = AVAudioEngine()
    private var node: AVAudioSourceNode?
    private var timer: Timer?
    private var cancelExport = false

    /// Text search over composer, title, tags, corpus and the description; every word must match. Combined with the challenge filter.
    var filtered: [Piece] {
        let words = searchText.lowercased().split(separator: " ").map(String.init)
        return pieces.filter { p in
            (tagFilter.map { p.tags.contains($0) } ?? true) && words.allSatisfy { w in
                (p.composer + " " + p.title + " " + p.tags.joined(separator: " ") + " " + p.corpus + " " + p.why).lowercased().contains(w)
            }
        }
    }
    var scene: RollScene { RollScene(song: song, time: time, sounding: sounding, range: excerptIn...max(excerptOut, excerptIn + 0.01), title: selection.map { "\($0.composer) · \($0.title)" } ?? "") }

    init() {
        synth.setOptions(options)
        let format = AVAudioFormat(standardFormatWithSampleRate: Synth.sampleRate, channels: 2)!
        let synth = self.synth
        let node = AVAudioSourceNode(format: format) { _, _, frameCount, abl -> OSStatus in
            let bufs = UnsafeMutableAudioBufferListPointer(abl)
            guard bufs.count >= 2, let l = bufs[0].mData, let r = bufs[1].mData else { return noErr }
            synth.render(frames: Int(frameCount), left: l.assumingMemoryBound(to: Float.self), right: r.assumingMemoryBound(to: Float.self))
            return noErr
        }
        engine.attach(node); engine.connect(node, to: engine.mainMixerNode, format: format); self.node = node
    }

    func select(_ piece: Piece) {
        pause(); selection = piece
        do {
            song = try synth.load(url: piece.url)
            if let e = piece.excerpt, e.count == 2 { excerptIn = min(e[0], song.duration); excerptOut = min(e[0] + e[1], song.duration) } else { excerptIn = 0; excerptOut = song.duration }
            synth.seek(excerptIn); time = excerptIn; synth.endTime = excerptOut
            status = "\(piece.composer) · \(piece.title) · \(song.notes.count) notes · \(fmt(song.duration))"
        } catch { status = error.localizedDescription; song = .empty }
    }
    func openFile() {
        let p = NSOpenPanel(); p.allowedContentTypes = [.midi]; p.allowsMultipleSelection = false
        if p.runModal() == .OK, let url = p.url {
            var s = pf_song_probe(url)
            let piece = Piece.fromFile(url, duration: s.0, notes: s.1); s = (0, 0)
            pieces.insert(piece, at: 0); select(piece)
        }
    }
    private func pf_song_probe(_ url: URL) -> (Double, Int) {
        if let r = try? OfflineRenderer(url: url, start: 0, end: 1, options: options) { return (r.duration, 0) }
        return (0, 0)
    }

    func togglePlay() { isPlaying ? pause() : play() }
    func play() {
        guard selection != nil else { return }
        if time >= excerptOut - 0.01 || time < excerptIn { synth.seek(excerptIn); time = excerptIn }
        synth.endTime = excerptOut
        do { try engine.start() } catch { status = "Audio engine: \(error.localizedDescription)"; return }
        isPlaying = true
        timer?.invalidate()
        timer = Timer.scheduledTimer(withTimeInterval: 1.0 / 30, repeats: true) { [weak self] _ in Task { @MainActor in self?.tick() } }
    }
    func pause() { let was = isPlaying; engine.pause(); isPlaying = false; timer?.invalidate(); timer = nil; if was { tick() } }
    func stop() { pause(); synth.seek(excerptIn); time = excerptIn; sounding = Array(repeating: false, count: 128) }
    func seek(_ t: Double) { synth.seek(t); time = t; sounding = synth.soundingKeys() }
    private func tick() {
        time = synth.time; sounding = synth.soundingKeys(); activeVoices = synth.activeVoices; dspLoad = synth.load; dspPeak = synth.loadPeak
        if synth.finished || time >= excerptOut { pause(); time = excerptOut }
    }
    func setIn() { excerptIn = min(time, excerptOut - 0.5) ; if time < excerptIn { seek(excerptIn) } }
    func setOut() { excerptOut = max(time, excerptIn + 0.5); synth.endTime = excerptOut }
    func wholePiece() { excerptIn = 0; excerptOut = song.duration; synth.endTime = excerptOut }
    func suggestedExcerpt() { if let p = selection { select(p) } }
    func fmt(_ t: Double) -> String { String(format: "%d:%05.2f", Int(t) / 60, t - Double(Int(t) / 60 * 60)) }

    func export(_ format: ExportFormat) {
        guard let piece = selection else { return }
        let panel = NSSavePanel()
        let base = "\(piece.composer.isEmpty ? "" : piece.composer + " - ")\(piece.title)".replacingOccurrences(of: "/", with: "-")
        panel.nameFieldStringValue = "\(base) [\(Int(excerptIn))-\(Int(excerptOut))s].\(format.ext)"
        guard panel.runModal() == .OK, let url = panel.url else { return }
        let req = ExportRequest(midi: piece.url, output: url, start: excerptIn, end: excerptOut, options: options, format: format, song: song, title: "\(piece.composer) · \(piece.title)")
        exporting = true; exportProgress = 0; cancelExport = false; status = "Exporting…"
        Task.detached(priority: .userInitiated) { [weak self] in
            do {
                try Exporter.run(req) { p, msg in
                    var keep = true
                    DispatchQueue.main.sync { guard let self else { keep = false; return }; self.exportProgress = p; self.status = msg; keep = !self.cancelExport }
                    return keep
                }
                await MainActor.run { self?.exporting = false; self?.status = "Saved \(url.lastPathComponent)"; NSWorkspace.shared.activateFileViewerSelecting([url]) }
            } catch {
                await MainActor.run { self?.exporting = false; self?.status = error.localizedDescription }
            }
        }
    }
    func cancel() { cancelExport = true }
}
