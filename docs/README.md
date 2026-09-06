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

Settings (⌘K / Ctrl+K): every synth control with its known-good default (the defaults
come from the wasm module, `pfw_default`), grouped, with the sympathetic-resonance
internals under *Experimental* and the older behaviours under *Legacy*.

Serve locally with any static server, e.g. `python3 -m http.server -d docs 8080`.

Licenses: performances and scores are from the ASAP / nASAP datasets (CC BY-NC-SA 4.0,
non-commercial; referenced by their dataset paths in `pieces.json`). Verovio is LGPL-3.0
(loaded from jsDelivr). The synth is MIT. Pianoteq was used only as a black-box listening /
measurement reference while fitting; nothing from it is included.
