# pfsynth web demo (GitHub Pages)

Static page: the partial-model piano compiled to WebAssembly, running in an
AudioWorklet, playing (n)ASAP performances while the score (rendered by Verovio
from the ASAP MusicXML) follows note by note through the nASAP note alignments.

Files

- `pfsynth.wasm` — built from `src/core/{pf_partial,pf_attack,pf_resonance}.c`,
  `src/host/{midi,pfplayer,pfwasm}.c` by `tools/build_wasm.sh` (needs wasi-sdk in
  `build/wasi/wasi-sdk`, see the script). The fitted patches are compiled in.
- `worklet.js` — AudioWorkletProcessor hosting the module (WASI stubs, 128-frame blocks,
  status messages with time / sounding keys / DSP load).
- `app.js`, `index.html`, `style.css` — the page. No build step.
- `pieces/<id>/{perf.mid, score.musicxml.gz, align.tsv.gz}` and `pieces.json` — written by
  `tools/web_pieces.py` from the local (n)ASAP corpus.

Score following: nASAP's `note_alignment.tsv` maps performed notes (pitch, onset) to the
`id` attributes nASAP added to the MusicXML notes; Verovio keeps those ids on its SVG
`g.note` elements, so highlighting is a lookup. Clicking a note seeks to its performance
onset.

Your own MIDI files: drop `.mid` files anywhere on the page (or *Open MIDI…*); they play
with a velocity-coloured piano roll instead of a score (no alignment exists for them) and
stay in the list for the session. Nothing is uploaded: the file is parsed in the browser.

Memory meter (bottom row): main-thread JS heap from Chrome's `performance.memory` (used, with
the peak and limit in the tooltip; other browsers show n/a), the synth's wasm linear memory as
reported by the worklet, and the DOM element count (score SVGs dominate). Sampled once a
second; the tooltip lists what the page retains (dropped files, event arrays, notes) to help
spot leaks.

Settings (⌘K / Ctrl+K): every synth control with its known-good default (the defaults
come from the wasm module, `pfw_default`), grouped, with the sympathetic-resonance
internals under *Experimental* and the older behaviours under *Legacy*.

Serve locally with any static server, e.g. `python3 -m http.server -d docs 8080`.

Licenses: performances and scores are from the ASAP / nASAP datasets (CC BY-NC-SA 4.0,
non-commercial; referenced by their dataset paths in `pieces.json`). Verovio is LGPL-3.0
(loaded from jsDelivr). The synth is MIT. Pianoteq was used only as a black-box listening /
measurement reference while fitting; nothing from it is included.

## Classical guitar (`guitar/`)

A second page, `guitar/index.html`, plays the physically modelled guitar live: `guitar/pfguitar.wasm`
(the instrument API, `pfiw_*` exports; built by `tools/build_guitar_wasm.sh`) runs in
`guitar-worklet.js`; the page applies the measured guitar body and a room as WebAudio
convolutions. Performances (`guitar/pieces/<slug>/score.json` + `score.musicxml.gz`, listed in
`guitar/pieces.json`) come from `tools/guitar_piece_build.py` and `tools/guitar_web_export.py`:
GAPS timing, strings and frets; velocities fitted to each recording with its room; the room's
three fitted numbers, regenerated in the browser as a stereo impulse response; the closest
measured body. Verovio engraves notation and tab one system at a time (one system per page,
tab rhythm symbols hidden); notes light in a hue for pluck strength, offset by the audio
output latency so they light when heard; right-hand fingers (p i m a) are drawn by the page.
`guitar/import.js` reads MusicXML (tab staff or `<technical>` strings/frets, repeats, ties,
slurs as hammer-ons/pull-offs, harmonics, dynamics, arpeggios, tempo; the guitar's octave
convention) and MIDI (strings from one channel per string, else chosen by the instrument).
The header shows the audio thread's CPU share. Source and license information is in each
piece's credits; body responses are CC BY 4.0 (R. Mores). Recordings are not included.

Recuerdos de la Alhambra uses an independent conversion of Stewart Holmes's Mutopia #810
edition (CC BY-SA 3.0), inferred tablature, and the accepted Recuerdos phrasing studies.
Its full performance includes repeats, regular p–a–m–i subdivisions and phrase rubato.
The optional JSON timing/finger controls and their streaming synthesis behavior are
explained in [Guitar performance JSON](guitar/FORMAT.md). The shared piano/fiddle
`pfi.wasm` is unchanged. See `guitar/pieces/tarrega-recuerdos/SOURCE.txt` for the
score adaptation, hand-audit assumptions, body-loading source and tone-port limits.


### Checking recording-driven guitar fits

A downloaded performance need not share the GAPS MIDI clock. Known corrections in
`tools/guitar_reference_offsets.json` are keyed to the SHA-256 of the exact decoded
audio. The Carulli reference begins around 0.888 seconds in our local recording,
while its MIDI begins at 2.636 seconds. A different audio hash requires a new
alignment check; the tool refuses to apply the correction to another file.

The piece builder records MIDI and audio origins separately, and now rejects
collapsed or non-improving dynamics after applying the physical pluck cap. These
numerical checks do not replace blind listening acceptance.

For Carulli, the recording and full-resolution GAPS files can be fetched locally
with `tools/gaps_fetch.py 055_cV1wc`. Its prerequisites are the [GAPS v1 archive](https://zenodo.org/records/13962272)
at `research/strings/downloads/gaps/gaps_v1_no_audio.zip`, and the archive's
`gaps_v1/gaps_v1_metadata.csv` copied to `research/strings/midi/gaps/metadata.csv`;
`yt-dlp` and `ffmpeg` fetch/decode audio. These inputs remain git-ignored.
The refitting command itself does not require this archive or the private MIDI/XML.
To refit using
only the public performance notes plus the local recording:

```sh
OPENBLAS_NUM_THREADS=1 build/body-venv/bin/python tools/carulli_recording_refit.py \
  --recording research/strings/midi/gaps/carulli-op241-5/cV1wc.wav \
  --output build/carulli-refit-new
python3 tools/test_guitar_reference.py
```

The output directory must be new and under ignored `build/`. The fit writes local
candidate audio and JSON; it does not replace the public score. Optional `--events`
accepts the original unrounded local events for research reproduction. No reference
recordings or raw GAPS assets belong in a PR. The corrected Carulli fit was
preferred in all four owner blind comparisons on 2026-10-08; its pluck strengths
and drier room now replace the temporary velocity floor in the bundled score.
See `experiments/string-gestures/pieces/carulli-op241-5-accepted-2026-10-08.json`
for the verdicts and audio checksum. This is a piece-specific acceptance, not
approval of future automatically generated fits or a global guitar retune.

If another download decodes to a different audio hash, verify its timing instead
of reusing the correction blindly. `--alignment-manifest local-offsets.json` accepts
the same schema as `tools/guitar_reference_offsets.json`, with the new exact hash,
verified `audio_origin_seconds`, and a description of the alignment check. Keep
that file local until its provenance has been reviewed.
