# Experiment 03: onset model (`pf_attack`) against Pianoteq

Target changed at the user's request: **Pianoteq 6 "Steinway D Close Mic Classical"** (rendered
headless from MIDI by `tools/ptq_render.py`, cached in `calib/pianoteq-ref/closemic/`) replaces the
Salamander as the reference for this experiment. Pianoteq is used strictly as a black box: it is
rendered and measured; nothing is copied from it. The Salamander-fitted tonal baseline
(`experiments/partial-piano/patch.bin`) is preserved unchanged.

## What the measurements said (tools/attack_analysis.py, attack_lf.py, PF_REF=ptq)

Against the frozen tonal model, isolated Pianoteq notes differ at onset in two ways:

1. **Sub-fundamental "thump".** Under every note there is a set of shared low modes (about
   59, 65, 76, 88, 95, 118, 125, 153, 158, 168, 180, 199, 207, 231, 260, 270 Hz ...) with T60
   0.6-1.0 s (no room tail in the close-mic preset). Its level is roughly register-independent,
   grows with velocity, and sits 15-30 dB under the fundamental for C4-C6. The tonal model has
   nothing below the fundamental. (The Salamander shows the same component with 1-2.5 s room tails.)
2. **Onset sharpness.** Pianoteq reaches full level within ~2 ms of onset. The tonal model's
   envelope knots were measured with a +-8 ms window that straddled the 5 ms lead-in the sample
   loader keeps, so the model is 10-20 dB soft in its first 10 ms (C4-C5).

A hard-mask HPSS "percussive" component (the STN paper's transient target) is essentially empty for
both pianos; the between-partial noise above 1 kHz in 10-40 ms is 30+ dB under the tone.

## The model

`src/core/pf_attack.{h,c}` (portable C, no allocation) adds per note:

* one bank of up to 64 second-order resonators, each struck by a unit-area Hann pulse at least
  0.5 ms and at least half the mode's period long (a gentle strike for the low modes): 28 **slow** modes
  at the measured Pianoteq frequencies/decays plus **fast "knock"** modes (T60 0.35 s) at the
  log-midpoints between them and up to 1 kHz. Neighbouring modes alternate sign (mixed-sign
  radiation of a soundboard's modes); with equal signs the dense bank cancels between resonances and
  its band response has 25 dB notches, which the fitter then over-compensated. Each mode has its own
  strike delay (the lowest modes peak ~14 ms after the tone in the reference; higher ones at 0-6 ms).
  Pulse level per note is a register x velocity table (dB), fitted where the sub-fundamental region
  is observable (C3-C6 plus F#3-F#5) and held constant below C3; it is interpolated linearly in MIDI
  velocity, which follows Pianoteq's measured thump-vs-velocity curve within ~1 dB from v30 to v120.
* Because the reference's low thump is two-stage (a 100-200 ms burst, then a long quiet tail) and
  varies +-7 dB from key to key, single exponentials extrapolated from the late slope misplace
  energy: a first render carried ~12 dB too much 40-80 Hz over the whole passage. The fit therefore
  ends with a closed-loop pass that corrects every mode so the total 0-200 ms energy per
  sub-fundamental band matches the reference on average (converges to +-0.5 dB in three passes).
  The 0-200 ms window was chosen because the passage has an onset every ~150 ms, so the early
  part of each thump is what the mix (and the ear) integrates; the 0-500 ms version left the
  passage 5 dB too heavy at 40-160 Hz. A final -3 dB trim (`thump_mix` in the patch) calibrates
  the bank against lone onsets *inside* the passage at its actual velocities (40-77), which still
  measured +4 dB against Pianoteq's in-passage thumps after the isolated-note fit.
* a filtered-noise burst (two cascaded one-poles, exponential envelope) fitted to the between-partial
  floor above 1 kHz. It is small (-42 to -50 dBFS peak for loud notes) and probably inaudible.

Fit: `PF_REF=ptq tools/attack_fit.py` -> `attack.json`, `patch_attack.h`. Closed loop:
`PF_REF=ptq tools/attack_verify.py` renders tonal+attack and prints band levels per onset window
against the reference (held-out A4/A5 included). After the fixes above, the sub-fundamental bands
match within ~5 dB per onset window for most notes; individual keys still deviate by up to 10 dB in
a given window because the shared bank reproduces the average key, not each key. Passage-level
checks (40-160 Hz per 250 ms frame, and lone onsets inside the passage vs. their isolated Pianoteq
renders) are reported in the session summary; Pianoteq's in-passage thumps equal its isolated ones
and do not depend on pedal or hold time (tested 0.1-8 s).

The kernel passes block-size equivalence (257 vs 4096 samples) and finite-output checks at MIDI
21/36/60/84/108, velocities 0.01 and 1.

## Pianoteq-fitted tonal patch (`experiments/partial-piano-ptq/`)

`tools/partial_fit_ptq.py` reruns the experiment-02 identification on the Pianoteq notes with
**onset-exact** envelopes (recording shifted to its detected onset; first knot over 0-6 ms). Its
first 20 ms now match Pianoteq within 2-3 dB (C4/C5 v100) where the baseline was 10-20 dB low.
Same 12,744-byte format; the frozen Salamander patch is untouched.

## Audition (audition/, two new clips, existing clips/votes untouched)

Same Beethoven Op. 2 No. 1 event list and 240 ms damper release as experiment 02. Reference for
both: Pianoteq rendering the same MIDI (original key releases + CC64; notes still down at the
excerpt boundary released there).

* `beethoven-attack`: A/B = frozen tonal baseline vs baseline + `pf_attack`.
* `beethoven-ptq-fit`: A/B = baseline + attack vs Pianoteq-fitted onset-exact tone + attack.

`tools/attack_audition.py` renders both (whole-clip RMS matching per clip) and writes
`beethoven-report.json` with source/patch/kernel hashes and peak levels.

## Honest status

Physically modelled: shared modal soundboard response struck impulsively per note; velocity-scaled
excitation. Empirically fitted: mode frequencies/decays/weights, per-register levels, noise floor.
Not modelled: hammer-string contact physics, key/action noise as a separate mechanism, sympathetic
coupling, release noise. The hiss module is retained but likely below audibility. The bass anchors'
excitation level is an extrapolation. Judge by the Beethoven A/B, not by these band tables.

## Per-key body weights and live trims (2026-09-05)

Pianoteq's body-mode weights change from key to key by +-5..10 dB (e.g. the 65 Hz mode is +8 dB
under D4-F4 and -7 dB under B4); a single averaged bank plays the same signature on every note.
`mode_db_key[anchor][mode]` now stores per-anchor offsets on the shared weights, taken from the
per-note residuals of the rank-1 fit (slow modes) and of the knock band fit (fast modes) and
interpolated onto the A0..C8 grid. The patch also carries `slow_mix` / `knock_mix` trims; the
player exposes them with the noise trim as dB offsets (`--body`, `--knock`, `--noise` in the
demo app's headless mode; sliders in the app), so each component can be tuned or muted by ear.
