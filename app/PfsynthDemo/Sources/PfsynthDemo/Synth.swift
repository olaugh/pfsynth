import Foundation
import PfsynthCore

struct NoteEvent: Hashable { let start: Double; let end: Double; let pitch: Int; let velocity: Int }
struct PedalPoint: Hashable { let t: Double; let value: Int }
struct SongData {
    var notes: [NoteEvent] = []; var sustain: [PedalPoint] = []; var soft: [PedalPoint] = []; var sostenuto: [PedalPoint] = []
    var duration: Double = 0; var eventCount = 0
    static let empty = SongData()
}
/// Every knob of the player with its known-good default (the values a fresh `SynthOptions()` carries).
struct SynthOptions: Equatable {
    var pianoteqTone = true; var attack = true; var continuousPedal = true; var unaCorda = true; var gainDb = 6.0
    var bodyDb = -18.0, knockDb = -22.0, noiseDb = -17.0, trebleDb = 0.0   // onset trims (below A#5; trebleDb from G#6 up) relative to the fit, chosen by ear on 2026-09-05 (experiments/attack-ptq/listening-trims.json)
    var resonance = true, resonanceDb = 0.0                // sympathetic string resonance (experiments/sympathetic), fitted to Pianoteq lone notes
    var resCoupling = -32.0, resSkirt = 1.5, resSustain = -15.0, resTilt = 0.0, resT60 = 1.0   // its internals (Experimental section)
    var topKnockT60 = 0.25, topKnockDb = 0.0   // very top register: decay scale / level of the knock modes just below the fundamental
    var c: pf_player_options {
        pf_player_options(tone: pianoteqTone ? 1 : 0, attack: attack ? 1 : 0, pedal_mode: continuousPedal ? 1 : 0, una_corda: unaCorda ? 1 : 0, gain: pow(10, gainDb / 20), body_db: bodyDb, knock_db: knockDb, noise_db: noiseDb, treble_db: trebleDb, top_knock_t60: topKnockT60, top_knock_db: topKnockDb, top_knock_lo: 0.5, limiter: 1, resonance: resonance ? 1 : 0, resonance_db: resonanceDb)
    }
    var resonanceParams: pf_resonance_params {
        var r = pf_resonance_params(); pf_resonance_defaults(&r)
        r.coupling_db = Float(resCoupling); r.skirt_hz = Float(resSkirt); r.sustain_db = Float(resSustain); r.tilt_db = Float(resTilt); r.t60_scale = Float(resT60)
        return r
    }
    var resonanceInternals: [Double] { [resCoupling, resSkirt, resSustain, resTilt, resT60] }
}
enum SynthError: Error, LocalizedError {
    case load(String), export(String)
    var errorDescription: String? { switch self { case .load(let s): return "Could not load MIDI: \(s)"; case .export(let s): return s } }
}

/// Digest the loader's flat event list into notes and pedal curves for display.
func digest(_ s: pf_song) -> SongData {
    var d = SongData(); d.duration = s.duration; d.eventCount = Int(s.n)
    var open: [Int: [(Double, Int)]] = [:]
    for i in 0..<Int(s.n) {
        let e = s.ev[i]; let p = Int(e.note); let v = Int(e.val); let t = e.t
        switch Int(e.type) {
        case PF_EV_NOTE_ON: open[p, default: []].append((t, v))
        case PF_EV_NOTE_OFF: if var q = open[p], !q.isEmpty { let (t0, v0) = q.removeFirst(); open[p] = q; d.notes.append(NoteEvent(start: t0, end: t, pitch: p, velocity: v0)) }
        case PF_EV_PEDAL: d.sustain.append(PedalPoint(t: t, value: v))
        case PF_EV_SOSTENUTO: d.sostenuto.append(PedalPoint(t: t, value: v))
        case PF_EV_SOFT: d.soft.append(PedalPoint(t: t, value: v))
        default: break
        }
    }
    for (p, q) in open { for (t0, v0) in q { d.notes.append(NoteEvent(start: t0, end: s.duration, pitch: p, velocity: v0)) } }
    d.notes.sort { $0.start < $1.start }
    return d
}

/// Real-time synth: one pf_player behind a lock, pulled by the audio thread.
final class Synth {
    static let sampleRate = 44100.0
    private var song = pf_song(); private var loaded = false
    private let player = UnsafeMutablePointer<pf_player>.allocate(capacity: 1)
    private let lock = NSLock()
    private var mono = [Float](repeating: 0, count: 16384)
    private var opts = SynthOptions().c
    var endTime: Double = .infinity
    private(set) var finished = false
    /// DSP load: fraction of the audio callback's time budget spent rendering (smoothed) and its recent peak; > 1 means dropouts.
    private(set) var load = 0.0, loadPeak = 0.0; private var peakAge = 0

    init() { pf_player_init(player, Synth.sampleRate, &opts) }
    deinit { if loaded { pf_midi_free(&song) }; player.deallocate() }

    func load(url: URL) throws -> SongData {
        lock.lock(); defer { lock.unlock() }
        var s = pf_song()
        guard pf_midi_load(&s, url.path) == 0 else { throw SynthError.load(url.lastPathComponent) }
        if loaded { pf_midi_free(&song) }
        song = s; loaded = true; finished = false
        pf_player_load(player, song.ev, song.n, song.duration)
        return digest(song)
    }
    private var internals = SynthOptions().resonanceInternals
    func setOptions(_ o: SynthOptions) {
        lock.lock(); opts = o.c; pf_player_set_options(player, &opts)
        if o.resonanceInternals != internals { internals = o.resonanceInternals; var r = o.resonanceParams; pf_player_set_resonance(player, &r) }   // rebuilds the bank (silences it)
        lock.unlock()
    }
    func seek(_ t: Double) { lock.lock(); pf_player_seek(player, t); finished = false; lock.unlock() }
    var time: Double { lock.lock(); defer { lock.unlock() }; return pf_player_time(player) }
    var activeVoices: Int { lock.lock(); defer { lock.unlock() }; return Int(pf_player_active(player)) }
    func soundingKeys() -> [Int] {
        var keys = [UInt8](repeating: 0, count: 128)
        lock.lock(); _ = pf_player_sounding(player, &keys); lock.unlock()
        return keys.map { Int($0) }
    }
    /// Called from the audio thread.
    func render(frames: Int, left: UnsafeMutablePointer<Float>, right: UnsafeMutablePointer<Float>) {
        lock.lock(); defer { lock.unlock() }
        if !loaded || pf_player_time(player) >= endTime {
            if loaded { finished = true }
            for i in 0..<frames { left[i] = 0; right[i] = 0 }
            return
        }
        if mono.count < frames { mono = [Float](repeating: 0, count: frames) }
        let t0 = DispatchTime.now().uptimeNanoseconds
        mono.withUnsafeMutableBufferPointer { _ = pf_player_render(player, $0.baseAddress, Int32(frames)) }
        let used = Double(DispatchTime.now().uptimeNanoseconds - t0) / 1e9 / (Double(frames) / Synth.sampleRate)
        load += (used - load) * 0.2
        if used >= loadPeak { loadPeak = used; peakAge = 0 } else { peakAge += 1; if peakAge > 90 { loadPeak = max(used, loadPeak * 0.98) } }   // ~2 s hold, then decay
        for i in 0..<frames { left[i] = mono[i]; right[i] = mono[i] }
    }
}

/// Offline renderer with its own player: steps in blocks and reports which keys sound.
final class OfflineRenderer {
    private var song = pf_song()
    private let player = UnsafeMutablePointer<pf_player>.allocate(capacity: 1)
    let start: Double; let end: Double; let totalFrames: Int
    private(set) var rendered = 0
    init(url: URL, start: Double, end: Double, options: SynthOptions) throws {
        guard pf_midi_load(&song, url.path) == 0 else { throw SynthError.load(url.lastPathComponent) }
        var o = options.c; o.gain = 1; o.limiter = 0   // offline: float headroom, normalized afterwards
        pf_player_init(player, Synth.sampleRate, &o)
        if options.resonanceInternals != SynthOptions().resonanceInternals { var r = options.resonanceParams; pf_player_set_resonance(player, &r) }
        pf_player_load(player, song.ev, song.n, song.duration)
        self.start = max(0, start); self.end = min(end, song.duration)
        totalFrames = max(0, Int((self.end - self.start) * Synth.sampleRate))
        pf_player_seek(player, self.start)
    }
    deinit { pf_midi_free(&song); player.deallocate() }
    var duration: Double { song.duration }
    func render(into buf: UnsafeMutablePointer<Float>, frames: Int) { _ = pf_player_render(player, buf, Int32(frames)); rendered += frames }
    func soundingKeys() -> [Int] { var keys = [UInt8](repeating: 0, count: 128); _ = pf_player_sounding(player, &keys); return keys.map { Int($0) } }
    var time: Double { pf_player_time(player) }
}
