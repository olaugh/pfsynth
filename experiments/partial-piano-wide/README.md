# Wide-register tonal patches (C1–C8)

The experiment-02/03 patches had anchors from C2 to C6 only; every note above C6 reused the C6
envelope, inharmonicity and level with the frequency scaled, so the top octave rang far too long
(C8: 13 dB louder than Pianoteq 0.3 s after onset), carried C6's upper-partial balance, and the
bottom octave likewise borrowed C2. `pf_partial` now interpolates over 15 anchors, C1..C8 every
six semitones (`PF_PARTIAL_ANCHORS 15`), and `pf_attack` uses the same grid.

`tools/partial_fit_wide.py [salamander|pianoteq|both]` writes:

* `salamander.{bin,h}` — Salamander Grand samples, experiment-02 identification.
* `pianoteq.{bin,h}` — Pianoteq "Steinway D Close Mic Classical" renders, onset-exact identification.

The C2..C6 anchors are byte-identical to the frozen `experiments/partial-piano/patch.bin` and
`experiments/partial-piano-ptq/patch.bin` (verified), so everything judged so far is unchanged;
those two files keep the old 9-anchor layout and are no longer loadable by the current kernel.
Above C6 only 3–6 partials are measurable and the free inharmonicity search collapsed to B≈0 or ran
away, so for F#6..C8 the search is bounded around a stiffness law fitted to Pianoteq,
B ≈ 0.003 (f0/1047)^1.6, ×0.3..×3. Resulting B: Pianoteq 0.005 / 0.0075 / 0.019 / 0.049 and
Salamander 0.004 / 0.0066 / 0.011 / 0.042 at F#6 / C7 / F#7 / C8; the top notes are tuned 20-40
cents sharp (stretch), as in the sources. The damper model treats MIDI >= 90 as undamped strings.
The onset model was refitted with anchors F#6..C8 (Pianoteq's thump there is ~5 dB louder than at C6).
