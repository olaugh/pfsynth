import SwiftUI
import CoreGraphics

/// What the roll shows at one instant.
struct RollScene {
    var song: SongData
    var time: Double
    var window: Double = 8          // seconds visible across the roll
    var lead: Double = 0.22         // playhead position as a fraction of the roll width
    var sounding: [Bool] = Array(repeating: false, count: 128)
    var range: ClosedRange<Double>? // excerpt in/out
    var title = ""
}

/// Pure CoreGraphics renderer, shared by the SwiftUI canvas and the video exporter (y down).
struct RollRenderer {
    static let low = 21, high = 108
    static func isBlack(_ p: Int) -> Bool { [1, 3, 6, 8, 10].contains(p % 12) }

    func draw(_ s: RollScene, in ctx: CGContext, size: CGSize) {
        let W = size.width, H = size.height
        let kbW: CGFloat = 64, laneH: CGFloat = 76, topH: CGFloat = 26
        let rollY0 = topH, rollH = H - laneH - topH
        let keys = CGFloat(RollRenderer.high - RollRenderer.low + 1), kh = rollH / keys
        let rollX0 = kbW, rollW = W - kbW, pps = rollW / CGFloat(s.window), phx = rollX0 + rollW * CGFloat(s.lead)
        func y(_ pitch: Int) -> CGFloat { rollY0 + rollH - CGFloat(pitch - RollRenderer.low + 1) * kh }
        func x(_ t: Double) -> CGFloat { phx + CGFloat(t - s.time) * pps }

        ctx.setFillColor(CGColor(red: 0.075, green: 0.078, blue: 0.09, alpha: 1)); ctx.fill(CGRect(origin: .zero, size: size))
        // row stripes for black keys, octave lines
        for p in RollRenderer.low...RollRenderer.high {
            if RollRenderer.isBlack(p) { ctx.setFillColor(CGColor(gray: 1, alpha: 0.035)); ctx.fill(CGRect(x: rollX0, y: y(p), width: rollW, height: kh)) }
            if p % 12 == 0 { ctx.setStrokeColor(CGColor(gray: 1, alpha: 0.10)); ctx.setLineWidth(1); ctx.move(to: CGPoint(x: rollX0, y: y(p) + kh)); ctx.addLine(to: CGPoint(x: W, y: y(p) + kh)); ctx.strokePath() }
        }
        // excerpt range shading
        if let r = s.range {
            ctx.setFillColor(CGColor(gray: 0, alpha: 0.45))
            let a = x(r.lowerBound), b = x(r.upperBound)
            if a > rollX0 { ctx.fill(CGRect(x: rollX0, y: rollY0, width: min(a, W) - rollX0, height: rollH)) }
            if b < W { ctx.fill(CGRect(x: max(b, rollX0), y: rollY0, width: W - max(b, rollX0), height: rollH)) }
        }
        // seconds grid
        let t0 = s.time - Double(s.lead) * s.window, t1 = t0 + s.window
        var tick = floor(t0)
        while tick <= t1 {
            let xx = x(tick)
            if xx >= rollX0 { ctx.setStrokeColor(CGColor(gray: 1, alpha: tick.truncatingRemainder(dividingBy: 5) == 0 ? 0.16 : 0.06)); ctx.move(to: CGPoint(x: xx, y: rollY0)); ctx.addLine(to: CGPoint(x: xx, y: rollY0 + rollH)); ctx.strokePath()
                if tick.truncatingRemainder(dividingBy: 5) == 0 { label(ctx, String(format: "%d:%02d", Int(tick) / 60, Int(tick) % 60), at: CGPoint(x: xx + 3, y: 4), size: 10, color: CGColor(gray: 1, alpha: 0.5)) } }
            tick += 1
        }
        // notes
        for n in s.song.notes {
            if n.end < t0 - 0.05 || n.start > t1 { if n.start > t1 { break } else { continue } }
            let x0 = max(x(n.start), rollX0), x1 = min(x(n.end), W); if x1 <= x0 { continue }
            let live = n.start <= s.time && s.time <= n.end + 0.05
            let v = CGFloat(n.velocity) / 127
            let hue = 0.55 - 0.35 * v   // quiet = blue, loud = orange
            let color = live ? NSColor(hue: hue, saturation: 0.9, brightness: 1.0, alpha: 1).cgColor : NSColor(hue: hue, saturation: 0.75, brightness: 0.45 + 0.45 * v, alpha: 0.95).cgColor
            ctx.setFillColor(color)
            let rect = CGRect(x: x0, y: y(n.pitch) + 0.5, width: max(x1 - x0, 2), height: max(kh - 1, 1))
            ctx.addPath(CGPath(roundedRect: rect, cornerWidth: 1.5, cornerHeight: 1.5, transform: nil)); ctx.fillPath()
        }
        // playhead
        ctx.setStrokeColor(CGColor(gray: 1, alpha: 0.9)); ctx.setLineWidth(1.5); ctx.move(to: CGPoint(x: phx, y: rollY0)); ctx.addLine(to: CGPoint(x: phx, y: H)); ctx.strokePath()
        // keyboard
        ctx.setFillColor(CGColor(gray: 0.92, alpha: 1)); ctx.fill(CGRect(x: 0, y: rollY0, width: kbW, height: rollH))
        for p in RollRenderer.low...RollRenderer.high where !RollRenderer.isBlack(p) {
            ctx.setStrokeColor(CGColor(gray: 0.6, alpha: 1)); ctx.setLineWidth(0.5); ctx.move(to: CGPoint(x: 0, y: y(p) + kh)); ctx.addLine(to: CGPoint(x: kbW, y: y(p) + kh)); ctx.strokePath()
            if s.sounding[p] { ctx.setFillColor(CGColor(red: 1, green: 0.55, blue: 0.15, alpha: 1)); ctx.fill(CGRect(x: 0, y: y(p), width: kbW - 1, height: kh)) }
            if p % 12 == 0 { label(ctx, "C\(p / 12 - 1)", at: CGPoint(x: 3, y: y(p) - 1), size: max(7, min(10, kh * 0.9)), color: CGColor(gray: 0.35, alpha: 1)) }
        }
        for p in RollRenderer.low...RollRenderer.high where RollRenderer.isBlack(p) {
            ctx.setFillColor(s.sounding[p] ? CGColor(red: 1, green: 0.45, blue: 0.1, alpha: 1) : CGColor(gray: 0.12, alpha: 1))
            ctx.fill(CGRect(x: kbW * 0.42, y: y(p) + 0.5, width: kbW * 0.58 - 1, height: max(kh - 1, 1)))
        }
        // pedal lanes
        let laneY = H - laneH
        ctx.setFillColor(CGColor(gray: 0.05, alpha: 1)); ctx.fill(CGRect(x: 0, y: laneY, width: W, height: laneH))
        ctx.setFillColor(CGColor(red: 0.3, green: 0.6, blue: 1, alpha: 0.10)); ctx.fill(CGRect(x: rollX0, y: laneY + laneH * (1 - 84.0 / 127), width: rollW, height: laneH * 24.0 / 127))  // half-pedal zone 60-84
        drawCurve(ctx, s.sustain(t0: t0, t1: t1), x: x, laneY: laneY, laneH: laneH, fill: CGColor(red: 0.3, green: 0.6, blue: 1, alpha: 0.45), stroke: CGColor(red: 0.5, green: 0.75, blue: 1, alpha: 1), t0: t0, t1: t1, rollX0: rollX0, W: W)
        drawCurve(ctx, s.soft(t0: t0, t1: t1), x: x, laneY: laneY, laneH: laneH, fill: nil, stroke: CGColor(red: 1, green: 0.65, blue: 0.2, alpha: 1), t0: t0, t1: t1, rollX0: rollX0, W: W)
        drawCurve(ctx, s.sostenuto(t0: t0, t1: t1), x: x, laneY: laneY, laneH: laneH, fill: nil, stroke: CGColor(red: 0.6, green: 1, blue: 0.5, alpha: 1), t0: t0, t1: t1, rollX0: rollX0, W: W)
        label(ctx, "sustain", at: CGPoint(x: 6, y: laneY + 4), size: 10, color: CGColor(red: 0.5, green: 0.75, blue: 1, alpha: 1))
        label(ctx, "soft", at: CGPoint(x: 6, y: laneY + 18), size: 10, color: CGColor(red: 1, green: 0.65, blue: 0.2, alpha: 1))
        label(ctx, "sost.", at: CGPoint(x: 6, y: laneY + 32), size: 10, color: CGColor(red: 0.6, green: 1, blue: 0.5, alpha: 1))
        if !s.title.isEmpty { label(ctx, s.title, at: CGPoint(x: kbW + 8, y: 5), size: 13, color: CGColor(gray: 1, alpha: 0.85), bold: true) }
    }
    private func drawCurve(_ ctx: CGContext, _ pts: [PedalPoint], x: (Double) -> CGFloat, laneY: CGFloat, laneH: CGFloat, fill: CGColor?, stroke: CGColor, t0: Double, t1: Double, rollX0: CGFloat, W: CGFloat) {
        guard !pts.isEmpty else { return }
        let path = CGMutablePath(); var first = true
        func yv(_ v: Int) -> CGFloat { laneY + laneH - 2 - CGFloat(v) / 127 * (laneH - 6) }
        var last = pts[0]
        for p in pts {
            let px = max(min(x(p.t), W), rollX0)
            if first { path.move(to: CGPoint(x: rollX0, y: yv(p.value))); first = false }
            path.addLine(to: CGPoint(x: px, y: yv(last.value))); path.addLine(to: CGPoint(x: px, y: yv(p.value))); last = p
        }
        path.addLine(to: CGPoint(x: W, y: yv(last.value)))
        if let fill {
            let area = path.mutableCopy()!; area.addLine(to: CGPoint(x: W, y: laneY + laneH)); area.addLine(to: CGPoint(x: rollX0, y: laneY + laneH)); area.closeSubpath()
            ctx.setFillColor(fill); ctx.addPath(area); ctx.fillPath()
        }
        ctx.setStrokeColor(stroke); ctx.setLineWidth(1.2); ctx.addPath(path); ctx.strokePath()
    }
    private func label(_ ctx: CGContext, _ text: String, at p: CGPoint, size: CGFloat, color: CGColor, bold: Bool = false) {
        let font = CTFontCreateWithName((bold ? "HelveticaNeue-Bold" : "HelveticaNeue") as CFString, size, nil)
        let attrs: [CFString: Any] = [kCTFontAttributeName: font, kCTForegroundColorAttributeName: color]
        let line = CTLineCreateWithAttributedString(NSAttributedString(string: text, attributes: attrs as [NSAttributedString.Key: Any]))
        ctx.saveGState(); ctx.textMatrix = .identity; ctx.translateBy(x: p.x, y: p.y + size); ctx.scaleBy(x: 1, y: -1); CTLineDraw(line, ctx); ctx.restoreGState()
    }
}
extension RollScene {
    // the value in effect at t0 plus the points inside the window
    private func slice(_ pts: [PedalPoint], t0: Double, t1: Double) -> [PedalPoint] {
        var out: [PedalPoint] = []; var current = PedalPoint(t: t0, value: 0)
        for p in pts { if p.t < t0 { current = PedalPoint(t: t0, value: p.value) } else if p.t <= t1 { out.append(p) } else { break } }
        return [current] + out
    }
    func sustain(t0: Double, t1: Double) -> [PedalPoint] { slice(song.sustain, t0: t0, t1: t1) }
    func soft(t0: Double, t1: Double) -> [PedalPoint] { song.soft.isEmpty ? [] : slice(song.soft, t0: t0, t1: t1) }
    func sostenuto(t0: Double, t1: Double) -> [PedalPoint] { song.sostenuto.isEmpty ? [] : slice(song.sostenuto, t0: t0, t1: t1) }
}

struct PianoRollView: View {
    let scene: RollScene
    var body: some View {
        Canvas(rendersAsynchronously: false) { context, size in
            context.withCGContext { cg in RollRenderer().draw(scene, in: cg, size: size) }
        }
    }
}
