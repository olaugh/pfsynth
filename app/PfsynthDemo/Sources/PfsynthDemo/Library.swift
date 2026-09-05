import Foundation

struct PieceStats: Codable { let duration: Double; let notes: Int; let max_polyphony: Int; let notes_per_s: Double; let peak_notes_2s: Int; let vel_p5: Int; let vel_p95: Int; let repeated_rate: Double; let high_share: Double?; let notes_above_c7: Int? }
struct PiecePedal: Codable { let cc64: Int; let half_share: Double; let cc67: Int; let cc66: Int }
struct Piece: Codable, Identifiable, Hashable {
    let path: String; let composer: String; let title: String; let tags: [String]; let why: String
    let excerpt: [Double]?; let stats: PieceStats; let pedal: PiecePedal; let corpus: String; let license: String
    var id: String { path }
    var url: URL { URL(fileURLWithPath: path) }
    var exists: Bool { FileManager.default.fileExists(atPath: path) }
    static func == (a: Piece, b: Piece) -> Bool { a.path == b.path }
    func hash(into h: inout Hasher) { h.combine(path) }
    static func fromFile(_ url: URL, duration: Double, notes: Int) -> Piece {
        Piece(path: url.path, composer: "", title: url.deletingPathExtension().lastPathComponent, tags: ["custom"], why: "Opened from disk.", excerpt: nil,
              stats: PieceStats(duration: duration, notes: notes, max_polyphony: 0, notes_per_s: duration > 0 ? Double(notes)/duration : 0, peak_notes_2s: 0, vel_p5: 0, vel_p95: 0, repeated_rate: 0, high_share: 0, notes_above_c7: 0),
              pedal: PiecePedal(cc64: 0, half_share: 0, cc67: 0, cc66: 0), corpus: "file", license: "")
    }
}
enum Library {
    static let all: [Piece] = { (try? JSONDecoder().decode([Piece].self, from: Data(piecesJSON.utf8))) ?? [] }()
    static let tags = ["polyphony", "pedalling", "dynamics", "repeated notes", "high register"]
}
