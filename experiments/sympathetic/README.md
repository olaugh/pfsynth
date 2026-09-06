# Sympathetic string resonance (`src/core/pf_resonance`) — 2026-09-06

The partial-model voices are independent: nothing rang in sympathy. This experiment adds a
bank of per-string secondary resonators (Bank, Zambon & Fontana 2010) over the mix and fits
its two couplings to Pianoteq "Steinway D Close Mic Classical" as a black box.

## Measurement (`tools/symp_fit.py`)

Lone C3 / C4 / C5, velocity 80, held 3 s, sustain pedal fully DOWN before the note vs UP.
Energy at the note's own partials ("own") vs everything else in 30 Hz–8 kHz ("halo"):

| note | window | halo re own, pedal up | halo re own, pedal down | own partials down/up |
|---|---|---|---|---|
| C3 | 0.6–1.5 s | −48.3 dB | −27.9 dB | 0.0 dB |
| C3 | 1.5–3 s | −59.9 | −22.4 | 0.0 |
| C4 | 0.6–1.5 s | −43.1 | −20.5 | +0.5 |
| C4 | 1.5–3 s | −52.6 | −18.2 | +9.5 |
| C5 | 0.6–1.5 s | −43.4 | −18.3 | −1.1 |
| C5 | 1.5–3 s | −61.0 | −21.8 | +2.3 |

The halo sits at the *other strings' own* frequencies (for C4: 277.6, 247.6, 233.2, 509.7,
529.6 Hz — the semitone neighbours of the note's first two partials), is concentrated in the
long-ringing 120–1000 Hz strings, decays slower than the note (−9.7 vs −12.6 dB/s after
release with the pedal held), and strings whose partials coincide with the note's extend its
sustain (C4 +9.5 dB at 1.5–3 s).

## Model

Per string, one second-order resonator per partial (8), frequencies and free decay from the
tonal patch so they agree with the voices. Two couplings:

1. **Impulsive** — a note-on injects free vibration into every open string's resonators,
   proportional to the note's partial amplitudes seen through a Lorentzian skirt of width
   `skirt_hz` around each partial (6 dB per doubling of the detuning: reproduces the neighbour
   hierarchy). Coincident partial: `coupling_db` re the note's partial.
2. **Sustained** — the resonators are driven by the mix with a gain normalized so a partial
   exactly on a string mode builds up to at most `sustain_db` of the driver. An unbounded driven
   resonator would reach +60 dB (its resonant gain is 1/(1−r)) because nothing feeds energy
   back to the driver; the bound stands in for the exchange.

Dampers follow the voices' law (engagement from CC64, 80 dB/s seated, none from MIDI 90 up);
held and sostenuto-captured strings are open; a string being played is not driven.

## Fit

Grid over coupling / skirt / sustain / tilt (`grid.log`, `grid2.log`): best
`coupling −32 dB, skirt 1.5 Hz, sustain −15 dB, tilt 0` (error 164.9, from 24585 without the
bank). With it the model's halo is within 3 dB of Pianoteq in 7 of 9 windows (C4 1.5–3 s −13.3
vs −18.2; C5 0.15–0.6 −29.1 vs −22.9) and the octave-band distribution of the halo matches to
±4 dB. CPU: Debussy Reflets 9.9× → 8.4× real time offline.

A/B clips (dry / with resonance / Pianoteq): `tools/symp_audition.py` → `build/demo/symp-ab/`
(Chopin Op. 25/1, Debussy Reflets, Beethoven Op. 110, first 30–35 s).

Live: `pf_player_options.resonance` / `resonance_db`, `pfrender --no-resonance
--resonance-db --res-coupling --res-skirt --res-sustain --res-tilt --res-partials --res-t60`,
the demo app's Sound section and the web demo's settings palette (internals under Experimental).
