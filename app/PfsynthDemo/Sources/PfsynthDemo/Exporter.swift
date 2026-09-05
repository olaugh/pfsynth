import Foundation
import AVFoundation
import CoreGraphics
import CoreMedia
import CoreVideo

enum ExportFormat: String, CaseIterable, Identifiable { case mp3 = "MP3", m4a = "M4A (AAC)", mp4 = "MP4 video (piano roll)"; var id: String { rawValue }
    var ext: String { switch self { case .mp3: return "mp3"; case .m4a: return "m4a"; case .mp4: return "mp4" } } }

struct ExportRequest { let midi: URL; let output: URL; let start: Double; let end: Double; let options: SynthOptions; let format: ExportFormat; let song: SongData; let title: String }

enum Exporter {
    static let sr = Int32(Synth.sampleRate)
    static var mp3Encoder: URL? {
        for p in ["/opt/homebrew/bin/lame", "/usr/local/bin/lame", "/opt/homebrew/bin/ffmpeg", "/usr/local/bin/ffmpeg"] where FileManager.default.isExecutableFile(atPath: p) { return URL(fileURLWithPath: p) }
        return nil
    }

    /// Runs on a background task. progress in 0...1 with a status string; returns false from progress to cancel.
    static func run(_ req: ExportRequest, progress: @escaping (Double, String) -> Bool) throws {
        switch req.format {
        case .mp4: try exportVideo(req, progress: progress)
        case .mp3, .m4a:
            guard let audio = renderAudio(req, progress: { p in progress(p * 0.8, "Rendering audio…") }) else { throw SynthError.export("Cancelled") }
            if req.format == .m4a { try writeAAC(audio, to: req.output, fileType: .m4a); _ = progress(1, "Done") }
            else {
                guard let enc = mp3Encoder else { throw SynthError.export("No MP3 encoder found (install lame or ffmpeg via Homebrew). Use M4A instead.") }
                let tmp = FileManager.default.temporaryDirectory.appendingPathComponent("pfsynth-\(UUID().uuidString).wav")
                try writeWAV(audio, to: tmp); defer { try? FileManager.default.removeItem(at: tmp) }
                _ = progress(0.85, "Encoding MP3…")
                let proc = Process(); proc.executableURL = enc
                proc.arguments = enc.lastPathComponent == "lame" ? ["--quiet", "-q", "2", "-b", "256", tmp.path, req.output.path] : ["-y", "-loglevel", "error", "-i", tmp.path, "-codec:a", "libmp3lame", "-b:a", "256k", req.output.path]
                try proc.run(); proc.waitUntilExit()
                guard proc.terminationStatus == 0 else { throw SynthError.export("MP3 encoder failed (exit \(proc.terminationStatus))") }
                _ = progress(1, "Done")
            }
        }
    }

    /// Render the excerpt to mono float, peak-normalized to -1 dBFS.
    static func renderAudio(_ req: ExportRequest, progress: (Double) -> Bool) -> [Float]? {
        guard let r = try? OfflineRenderer(url: req.midi, start: req.start, end: req.end, options: req.options) else { return nil }
        var out = [Float](repeating: 0, count: r.totalFrames); var pos = 0; let block = 4096
        while pos < r.totalFrames {
            let n = min(block, r.totalFrames - pos)
            out.withUnsafeMutableBufferPointer { r.render(into: $0.baseAddress! + pos, frames: n) }
            pos += n
            if !progress(Double(pos) / Double(max(r.totalFrames, 1))) { return nil }
        }
        normalize(&out); return out
    }
    static func normalize(_ x: inout [Float]) {
        var peak: Float = 0; for v in x { peak = max(peak, abs(v)) }
        if peak > 0 { let g = Float(pow(10.0, -1.0 / 20)) / peak; for i in x.indices { x[i] *= g } }
    }
    static func writeWAV(_ x: [Float], to url: URL) throws {
        var d = Data(); let n = UInt32(x.count * 2)
        func u32(_ v: UInt32) { withUnsafeBytes(of: v.littleEndian) { d.append(contentsOf: $0) } }
        func u16(_ v: UInt16) { withUnsafeBytes(of: v.littleEndian) { d.append(contentsOf: $0) } }
        d.append(contentsOf: Array("RIFF".utf8)); u32(36 + n); d.append(contentsOf: Array("WAVE".utf8)); d.append(contentsOf: Array("fmt ".utf8)); u32(16); u16(1); u16(1); u32(UInt32(sr)); u32(UInt32(sr) * 2); u16(2); u16(16); d.append(contentsOf: Array("data".utf8)); u32(n)
        var pcm = [Int16](repeating: 0, count: x.count); for i in x.indices { pcm[i] = Int16(max(-32767, min(32767, (x[i] * 32767).rounded()))) }
        pcm.withUnsafeBufferPointer { d.append(UnsafeBufferPointer(start: UnsafeRawPointer($0.baseAddress!).assumingMemoryBound(to: UInt8.self), count: x.count * 2)) }
        try d.write(to: url)
    }

    // ---- CoreMedia plumbing for AAC
    static func audioFormat() -> CMAudioFormatDescription {
        var asbd = AudioStreamBasicDescription(mSampleRate: Double(sr), mFormatID: kAudioFormatLinearPCM, mFormatFlags: kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked, mBytesPerPacket: 4, mFramesPerPacket: 1, mBytesPerFrame: 4, mChannelsPerFrame: 1, mBitsPerChannel: 32, mReserved: 0)
        var fmt: CMAudioFormatDescription?
        CMAudioFormatDescriptionCreate(allocator: nil, asbd: &asbd, layoutSize: 0, layout: nil, magicCookieSize: 0, magicCookie: nil, extensions: nil, formatDescriptionOut: &fmt)
        return fmt!
    }
    static func sampleBuffer(_ x: UnsafePointer<Float>, frames: Int, at frame: Int64, format: CMAudioFormatDescription) -> CMSampleBuffer? {
        var bb: CMBlockBuffer?
        guard CMBlockBufferCreateWithMemoryBlock(allocator: nil, memoryBlock: nil, blockLength: frames * 4, blockAllocator: nil, customBlockSource: nil, offsetToData: 0, dataLength: frames * 4, flags: 0, blockBufferOut: &bb) == noErr, let bb else { return nil }
        CMBlockBufferReplaceDataBytes(with: x, blockBuffer: bb, offsetIntoDestination: 0, dataLength: frames * 4)
        var timing = CMSampleTimingInfo(duration: CMTime(value: 1, timescale: sr), presentationTimeStamp: CMTime(value: frame, timescale: sr), decodeTimeStamp: .invalid)
        var size = 4; var sb: CMSampleBuffer?
        CMSampleBufferCreate(allocator: nil, dataBuffer: bb, dataReady: true, makeDataReadyCallback: nil, refcon: nil, formatDescription: format, sampleCount: frames, sampleTimingEntryCount: 1, sampleTimingArray: &timing, sampleSizeEntryCount: 1, sampleSizeArray: &size, sampleBufferOut: &sb)
        return sb
    }
    static let aacSettings: [String: Any] = [AVFormatIDKey: kAudioFormatMPEG4AAC, AVSampleRateKey: 44100, AVNumberOfChannelsKey: 1, AVEncoderBitRateKey: 192_000]

    static func writeAAC(_ x: [Float], to url: URL, fileType: AVFileType) throws {
        try? FileManager.default.removeItem(at: url)
        let w = try AVAssetWriter(outputURL: url, fileType: fileType)
        let input = AVAssetWriterInput(mediaType: .audio, outputSettings: aacSettings); input.expectsMediaDataInRealTime = false
        guard w.canAdd(input) else { throw SynthError.export("Cannot add audio track") }; w.add(input)
        guard w.startWriting() else { throw SynthError.export(w.error?.localizedDescription ?? "writer failed") }
        w.startSession(atSourceTime: .zero)
        let fmt = audioFormat(); var pos = 0
        x.withUnsafeBufferPointer { p in
            while pos < x.count {
                let n = min(8192, x.count - pos)
                while !input.isReadyForMoreMediaData && w.status == .writing { usleep(1000) }
                if w.status != .writing { break }
                if let sb = sampleBuffer(p.baseAddress! + pos, frames: n, at: Int64(pos), format: fmt) { if !input.append(sb) { break } }
                pos += n
            }
        }
        input.markAsFinished()
        let sem = DispatchSemaphore(value: 0); w.finishWriting { sem.signal() }; sem.wait()
        if w.status != .completed { throw SynthError.export(w.error?.localizedDescription ?? "writer failed") }
    }

    // ---- MP4: piano roll frames + AAC audio, audio and picture in lockstep
    static func exportVideo(_ req: ExportRequest, progress: @escaping (Double, String) -> Bool) throws {
        let fps = 30, width = 1280, height = 720
        let r = try OfflineRenderer(url: req.midi, start: req.start, end: req.end, options: req.options)
        // two passes: normalize the audio first so the video's audio matches the audio-only exports
        var audio = [Float](repeating: 0, count: r.totalFrames); var pos = 0
        while pos < r.totalFrames { let n = min(4096, r.totalFrames - pos); audio.withUnsafeMutableBufferPointer { r.render(into: $0.baseAddress! + pos, frames: n) }; pos += n
            if !progress(0.35 * Double(pos) / Double(max(r.totalFrames, 1)), "Rendering audio…") { throw SynthError.export("Cancelled") } }
        normalize(&audio)
        let r2 = try OfflineRenderer(url: req.midi, start: req.start, end: req.end, options: req.options)   // again, for the key highlights
        try? FileManager.default.removeItem(at: req.output)
        let w = try AVAssetWriter(outputURL: req.output, fileType: .mp4)
        let vin = AVAssetWriterInput(mediaType: .video, outputSettings: [AVVideoCodecKey: AVVideoCodecType.h264, AVVideoWidthKey: width, AVVideoHeightKey: height,
            AVVideoCompressionPropertiesKey: [AVVideoAverageBitRateKey: 6_000_000, AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel]])
        vin.expectsMediaDataInRealTime = false
        let adaptor = AVAssetWriterInputPixelBufferAdaptor(assetWriterInput: vin, sourcePixelBufferAttributes: [kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA, kCVPixelBufferWidthKey as String: width, kCVPixelBufferHeightKey as String: height])
        let ain = AVAssetWriterInput(mediaType: .audio, outputSettings: aacSettings); ain.expectsMediaDataInRealTime = false
        guard w.canAdd(vin), w.canAdd(ain) else { throw SynthError.export("Cannot add tracks") }; w.add(vin); w.add(ain)
        guard w.startWriting() else { throw SynthError.export(w.error?.localizedDescription ?? "writer failed") }
        w.startSession(atSourceTime: .zero)
        let fmt = audioFormat(); let perFrame = Int(Synth.sampleRate) / fps  // 1470
        let nFrames = (r.totalFrames + perFrame - 1) / perFrame
        let cs = CGColorSpace(name: CGColorSpace.sRGB)!
        // Let the writer pull: each input asks for data on its own queue when it is ready.
        // (Polling isReadyForMoreMediaData on two inputs stalls: the writer interleaves them.)
        final class Box: @unchecked Sendable { var scene: RollScene; var frame = 0; var audioPos = 0; var error: Error?; var cancelled = false; let scratch: UnsafeMutablePointer<Float>
            init(scene: RollScene, n: Int) { self.scene = scene; scratch = .allocate(capacity: n) }; deinit { scratch.deallocate() } }
        let box = Box(scene: RollScene(song: req.song, time: req.start, range: req.start...req.end, title: req.title), n: perFrame)
        let group = DispatchGroup(); let aq = DispatchQueue(label: "pfsynth.export.audio"), vq = DispatchQueue(label: "pfsynth.export.video")
        let audioChunk = perFrame * 15
        group.enter()
        ain.requestMediaDataWhenReady(on: aq) {
            while ain.isReadyForMoreMediaData {
                if box.audioPos >= r.totalFrames || box.error != nil || box.cancelled { ain.markAsFinished(); group.leave(); return }
                let n = min(audioChunk, r.totalFrames - box.audioPos); var ok = false
                audio.withUnsafeBufferPointer { p in if let sb = sampleBuffer(p.baseAddress! + box.audioPos, frames: n, at: Int64(box.audioPos), format: fmt) { ok = ain.append(sb) } }
                if !ok { box.error = SynthError.export("Audio append failed: \(w.error?.localizedDescription ?? "unknown")"); ain.markAsFinished(); group.leave(); return }
                box.audioPos += n
            }
        }
        group.enter()
        vin.requestMediaDataWhenReady(on: vq) {
            while vin.isReadyForMoreMediaData {
                if box.frame >= nFrames || box.error != nil || box.cancelled { vin.markAsFinished(); group.leave(); return }
                let a0 = box.frame * perFrame, n = min(perFrame, r.totalFrames - a0)
                if n > 0 { r2.render(into: box.scratch, frames: n) }
                box.scene.time = req.start + Double(a0) / Synth.sampleRate; box.scene.sounding = r2.soundingKeys()
                guard let pool = adaptor.pixelBufferPool else { box.error = SynthError.export("No pixel buffer pool"); vin.markAsFinished(); group.leave(); return }
                var pb: CVPixelBuffer?; CVPixelBufferPoolCreatePixelBuffer(nil, pool, &pb)
                guard let pb else { box.error = SynthError.export("Pixel buffer allocation failed"); vin.markAsFinished(); group.leave(); return }
                CVPixelBufferLockBaseAddress(pb, [])
                if let ctx = CGContext(data: CVPixelBufferGetBaseAddress(pb), width: width, height: height, bitsPerComponent: 8, bytesPerRow: CVPixelBufferGetBytesPerRow(pb), space: cs, bitmapInfo: CGImageAlphaInfo.premultipliedFirst.rawValue | CGBitmapInfo.byteOrder32Little.rawValue) {
                    ctx.translateBy(x: 0, y: CGFloat(height)); ctx.scaleBy(x: 1, y: -1)
                    RollRenderer().draw(box.scene, in: ctx, size: CGSize(width: width, height: height))
                }
                CVPixelBufferUnlockBaseAddress(pb, [])
                if !adaptor.append(pb, withPresentationTime: CMTime(value: Int64(box.frame), timescale: Int32(fps))) { box.error = SynthError.export("Video append failed: \(w.error?.localizedDescription ?? "unknown")"); vin.markAsFinished(); group.leave(); return }
                box.frame += 1
                if box.frame % 15 == 0, !progress(0.35 + 0.65 * Double(box.frame) / Double(nFrames), "Encoding video…") { box.cancelled = true }
            }
        }
        group.wait()
        if box.cancelled { w.cancelWriting(); throw SynthError.export("Cancelled") }
        if let e = box.error { w.cancelWriting(); throw e }
        let sem = DispatchSemaphore(value: 0); w.finishWriting { sem.signal() }; sem.wait()
        if w.status != .completed { throw SynthError.export(w.error?.localizedDescription ?? "writer failed") }
        _ = progress(1, "Done")
    }
}
