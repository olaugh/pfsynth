# pfsynth demo app (macOS, SwiftUI)

Native macOS demo of the partial-model piano: `pf_partial` (Salamander- or Pianoteq-fitted tone on the C1–C8 anchor grid, `experiments/partial-piano-wide`),
`pf_attack` (soundboard thump + noise onset) and the continuous damper / una corda pedal model,
sequenced by the portable C player `src/host/pfplayer.c`. No Xcode project: a Swift package.

```
make app                      # swift build -c release + wrap as app/PfsynthDemo.app
open app/PfsynthDemo.app
make pfrender                 # CLI: build/pfrender in.mid out.wav [start end] [--tone sal|ptq] [--no-attack] [--pedal binary|continuous] [--no-soft]
```

Headless export (same code path as the buttons):
```
app/PfsynthDemo.app/Contents/MacOS/PfsynthDemo --export in.mid out.mp4 [start end] [--salamander] [--no-attack] [--binary-pedal] [--no-soft] [--gain dB]
```
`.mp3` (needs Homebrew `lame` or `ffmpeg`), `.m4a` (AAC, native) or `.mp4` (1280x720 30 fps piano
roll with keyboard highlights and sustain/soft/sostenuto lanes, H.264 + AAC, native AVFoundation).
Audio is peak-normalized to -1 dBFS.

## Library
`tools/curate_pieces.py` picks pieces from the local MAESTRO v3 and ASAP performances, computes
max polyphony, peak note density, velocity range, repeated-note rate and pedal usage, tags each
with its challenges (polyphony, pedalling, dynamics, repeated notes, high register) and writes `app/pieces.json`,
embedded as `PiecesData.swift`. Pieces reference the local files (CC BY-NC-SA 4.0); any MIDI can
be opened with ⌘O. Suggested excerpts are pre-set; Set in / Set out mark your own.

## Layout
- `PfsynthDemo/Sources/PfsynthCore`: symlink mirror of the C sources (keeps the repository's relative includes valid), umbrella header.
- `Synth.swift`: real-time player behind a lock (AVAudioSourceNode pulls it) and an offline renderer.
- `PianoRoll.swift`: one CoreGraphics renderer for the live canvas and the video frames.
- `Exporter.swift`: AAC/MP4 writing via AVAssetWriter; MP3 via lame/ffmpeg.
- `DemoModel.swift`, `ContentView.swift`: state, transport, options, export UI.

The engine and TUI in `src/host` still run the FDTD voice; this app is the first host of the partial model.
