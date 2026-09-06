# Full-keyboard tonal patches (A0–C8)

The experiment-02/03 patches had anchors from C2 to C6 only; every note above C6 reused the C6
envelope, inharmonicity and level with the frequency scaled, so the top octave rang far too long
(C8: 13 dB louder than Pianoteq 0.3 s after onset), carried C6's upper-partial balance, and the
bottom octave likewise borrowed C2. `pf_partial` now interpolates over 30 anchors, A0..C8 every
three semitones (`PF_PARTIAL_ANCHORS 30`, MIDI 21..108) — exactly the Salamander sample grid (A, C,
D#, F# in every octave), so no note is more than a whole tone from a measured anchor — and
`pf_attack` uses the same grid. (An intermediate C1..C8 six-semitone grid existed briefly.)

`tools/partial_fit_wide.py [salamander|pianoteq|both]` writes:

* `salamander.{bin,h}` — Salamander Grand samples, experiment-02 identification.
* `pianoteq.{bin,h}` — Pianoteq "Steinway D Close Mic Classical" renders, onset-exact identification.

The C and F# anchors from C2 to C6 are byte-identical to the frozen `experiments/partial-piano/patch.bin`
and `experiments/partial-piano-ptq/patch.bin` (verified); notes in between now interpolate from
closer anchors, so pitches that were previously three semitones from an anchor render slightly
differently (closer to the source). The old files keep the 9-anchor layout and are no longer
loadable by the current kernel. The A notes are no longer held out from fitting.
Above C6 only 3–6 partials are measurable and the free inharmonicity search collapsed to B≈0 or ran
away, so for F#6..C8 the search is bounded around a stiffness law fitted to Pianoteq,
B ≈ 0.003 (f0/1047)^1.6, ×0.3..×3. Resulting B: Pianoteq 0.005 / 0.0075 / 0.019 / 0.049 and
Salamander 0.004 / 0.0066 / 0.011 / 0.042 at F#6 / C7 / F#7 / C8; the top notes are tuned 20-40
cents sharp (stretch), as in the sources. The damper model treats MIDI >= 90 as undamped strings.
The onset model was refitted with anchors F#6..C8 (Pianoteq's thump there is ~5 dB louder than at C6).

## Bass onset (2026-09-05)

The low register sounded like a plucked bass: our notes reached -3 dB of peak 6 ms after onset at
every pitch, while Pianoteq's bass notes build over 20-40 ms (A0 42, E1 30, C2 31, G2 21, C3 23 ms;
~12 ms at C4, ~2 ms by C6). Two causes: below C3 the 20-90 ms demodulation window of the envelope
identification smears the rise away (fixed in `partial_fit_ptq.identify_envelopes` by scaling the
per-partial envelopes with the broadband 2 ms / smoothed envelope ratio for MIDI < 48), and the
onset layer fired at -20 dBFS in its first 4 ms. `pf_onset_seconds(f1)` now gives a register-
dependent raised-cosine onset (22 ms (131/f1)^0.4 below C3, 4 ms (131/f1)^1.2 + 2 ms above) applied
to both the tonal voice and `pf_attack`; measured time to -3 dB is now 37/33/38/23 ms at A0/E1/C2/G2
and unchanged from C4 up. Damper speed (73-87 dB/s), beating depth and content above the 64th
partial were checked in the bass and match Pianoteq within a few dB.
