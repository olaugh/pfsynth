# Experiment 04: half pedaling, una corda, sostenuto

Until now the sustain pedal was a switch (CC64 >= 64), releases were a fixed 240 ms exponential,
and CC67 (soft pedal) was dropped by the MIDI loader. Real performances use both continuously:
in the corpora surveyed below, 92% of CC64 events and 91% of CC67 events are intermediate values.

## Measurements (Pianoteq 6 "Steinway D Close Mic Classical", black box)

`tools/pedal_measure.py` renders single notes under fixed pedal values; per-partial envelopes
(complex demodulation at the fitted partial frequencies) give:

* **Damper engagement vs CC64.** Fraction of the full-damper effect (dB drop 300 ms after key
  release, net of the natural decay, mean over C3/C4/C5 and partial bands):
  CC64 <= 56: 1.0 · 64: 0.76 · 68: 0.77 · 72: 0.45 · 76: 0.22 · 80: 0.12 · 88: 0.01 · >= 96: 0.
  Fit: engagement = clamp((82 - CC64)/22)^0.95, i.e. the felt touches the string at position
  0.645 and is seated at 0.472 (`half-pedal-drops.json`, `pianoteq-pedal-measurements.json`).
* **Full-damper rate.** About 80 dB/s across partials once seated (77-96 dB/s for C3, 42-91 for
  C4, 40-74 for C5; slower for fundamentals of high notes, whose second polarization survives the
  damper). The previous fixed release removed 250 dB/s, three times too abrupt. Pianoteq's damping
  stalls about 45 dB under the level at release (C3 39, C4 45, C5 37 dB at 600 ms) and the residual
  then decays at its natural rate; the model reproduces this with a damper floor (`damp_floor_db`).
* **Una corda vs CC67** (s = CC67/127). Per-partial level change fits
  dL = -s^1.65 (2.10 + (1.66 + 0.13 (vel-80)/30) log2 k) dB (rms residual 2 dB): about -2.5 dB on
  the fundamental, -6 to -12 dB on partials 9+, more at high velocity; -2.5 to -4 dB RMS overall at
  full depth. Partials 1-2 decay 3-10 dB/s slower at C4/C5 (the unstruck third string sustains
  them), barely at C3.

## Model (`src/core/pf_partial.{h,c}`, new API; legacy path unchanged)

`pf_pedal_params` + `pf_partial_init2(..., pedal, soft)`, `pf_partial_pedal(v, position)`,
`pf_partial_release(v)` = key up. Every partial carries a damper gain that decays at
engagement(position) x rate(f) while the key is up, down to the floor; the pedal may move at any
time, so re-pedaling after a release simply stops the damping. Una corda scales the envelope knots per partial at
note-on and lets partials 1-2 lose 5 dB/s less (capped at 6 dB). `pf_partial_init` and the old
release behaviour are untouched, so experiments 02/03 still render bit-identically (verified by
re-rendering experiment 03 against the committed audio). Fitted defaults: `pf_pedal_defaults`.

Host: the loader keeps raw CC64 and adds `PF_EV_SOSTENUTO` (CC66) and `PF_EV_SOFT` (CC67); the
engine tracks a continuous pedal position (fed to `pf_symp`), soft depth, and sostenuto capture
(dampers of keys down at the press stay up until release). The FDTD voice still has a binary
damper, so the engine releases it below position 0.5; the new pedal model lives in `pf_partial`.

### Sostenuto
Needs its own **logic**, not a new sound model: at the press it captures the set of keys currently
down; those dampers stay up until the pedal is released, independent of later notes and of CC64.
Acoustically the captured strings behave like sustained notes (and should resonate sympathetically:
`pf_symp` would need per-string damper state for that). It is rare in the data: 369 of 2342 files
carry any CC66, mostly a handful of events (Balakirev Islamey 46, Schubert D.959 37, La Campanella 30).

## Corpus survey (`tools/pedal_survey.py`, `pedal-survey.json`)

2342 performances (MAESTRO v3 1276, ASAP 1066). Richest half pedaling (time with CC64 in 48-88):
Bach WTC fugues (GuoE01M bwv 883 85%, Shychko01M bwv 863 83%), Tchaikovsky Lullaby 76%, Schubert
Op. 90 76%, Beethoven Op. 110/I (Na06M) 75%, Andante favori 75%. Most una corda: Schubert Moment
Musical Op. 94/4, Scarlatti K. 213, Haydn Hob. XVI:50, Schubert Op. 90/3, Chopin Op. 25/2 (soft
pedal on 99-100% of the time). Both together: Scarlatti K. 213, Beethoven Op. 110/I (Kavalerova01),
Op. 10/2-3, Andante favori, Mozart K. 511, Schubert D. 960. `tools/pedal_excerpt.py` picks 25 s
windows with the most entries into Pianoteq's half-pedal zone (CC64 60-84) and a moving soft pedal.

## Audition (`tools/pedal_audition.py`, two clips)

* `pedal-op110`: Beethoven Op. 110/I coda, (n)ASAP Kavalerova01, 342.5 s to the next annotated
  downbeat (366.3 s); 98 notes, 585 CC64 and 147 CC67 moves.
* `pedal-andante`: Beethoven Andante favori WoO 57, MAESTRO 2018 Chamber6 track 20, 490-515 s;
  97 notes, 633 CC64 and 161 CC67 moves (soft pedal fully down at the start).

A = binary pedal (old behaviour), B = continuous damper + una corda, both on the Pianoteq-fitted
tone with the experiment-03 onset; reference = Pianoteq rendering the same events with all pedal
data. Whole-clip RMS matched. Hashes and pedal parameters: `audition-report.json`.

## Diagnostics on the clips
At the moments where Pianoteq's envelope drops more than 6 dB in 300 ms (its damping events), the
binary release drops 17 dB (Op. 110) / 12 dB (Andante) against Pianoteq's 10.7 / 9.2, the continuous
damper 10.7 / 7.1. Frame-wise, the change B-A points toward the reference with regression slope
~0.45 (1 would be exact). Whole-clip envelope error is dominated by other differences (dynamics,
sympathetic resonance) and cannot rank A vs B; judge by ear.

## Not modelled / caveats
The una corda decay change is
applied as a bounded gain, not a physical third string; the FDTD voice has no soft pedal; pedal
noise (see the session notes) is not added; sympathetic resonance is not in the offline renders.
