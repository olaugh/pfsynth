# Experiment 02: physics-informed tonal overhaul

The audition now opens on a 23-second Beethoven Sonata No. 1 excerpt (see below). **Small hours**, the original 13-second A-minor study at 104 BPM, remains available with melody, chords, bass, repeated notes, and a final cadence. The identical event list drives the current synth, the new model, and a sampled-piano reference. Eight isolated held-out A-note comparisons remain available. The old body-filter comparison is retained at `/body`, including its separate browser vote storage.

## Architectural choice

Based on Simionato, Fasciani & Holm, *Physics-informed Differentiable Method for Piano Modeling* (2024), Sections 3–4 and Figure 1, DOI 10.3389/frsip.2023.1276748. The downloaded paper and its original repository are in `research/piano`.

This is a new, independently implemented **adaptation of the signal model**, not a reproduction of the authors' neural estimator or a port of its trained weights. It replaces the old FDTD/hammer/body path with up to 64 inharmonic partials, fitted time-varying modal amplitudes, and two detuned branches. The frequency law uses measured first-partial frequency explicitly: `f_k = k*f1*sqrt((1+B*k*k)/(1+B))`. An offline estimator replaces the paper's LSTMs. The compact envelopes also incorporate the recorded radiation coloration; a separate body filter is not applied.

Differences from the paper: ten envelope knots instead of learned time-varying damping networks; a weak heuristic detuned branch; no double-frequency branch, noisy attack, or longitudinal interaction; a host-compatible release envelope added for musical comparisons. There is no nonlinear hammer-contact simulation or sympathetic bridge coupling. Thus this is a physics-informed parametric tone generator, not a complete physical piano simulator.

`src/core/pf_partial.c/.h` is portable C using only math/string facilities and no allocation. A patch stores nine C/F-sharp anchors, two velocities, 64 modes, ten half-dB envelope values per mode, and quantized phase. Tuning/B are float32. Intermediate pitches and velocities interpolate. The generated patch is **12,744 bytes raw / 6,821 bytes zlib-compressed**. This is parameter data only; no waveforms or neural weights are embedded in the kernel. It is not a measurement of final compressed Windows executable size.

## Fit and held-out design

Training notes: C2, F#2, C3, F#3, C4, F#4, C5, F#5, C6; Salamander velocity layers 6 and 13. The first partial and stiffness factor are identified from the loud note's partial peaks. Complex demodulation extracts amplitude envelopes at 0, 15, 40, 90, 200, 450, 900, 1800, 3600 and 6000 ms. Envelopes are quantized at 0.5 dB resolution and interpolated logarithmically. The nearest anchor supplies phase, avoiding interpolation across phase wraps.

A2/A3/A4/A5 at both velocities are excluded from fitting. The musical passage includes intermediate pitches and velocities, but is not a wholly held-out score: some C anchor notes occur in it. Register balance and musical dynamics are preserved; normalization is applied to each entire comparison clip, not to each note.

The reference is a sampled-piano reconstruction with nearest available notes shifted by at most a semitone and two-layer velocity interpolation. It is **not a pianist's recording**. All paths use the same note schedule and 240 ms release to -60 dB. The current FDTD path is rendered with its default body, then the same release envelope is applied offline. The new C model performs its release internally. No pedal or extra reverb is added. Single-note references preserve their recorded room response.

## Honest result

The bass spectral match improved substantially (A2: roughly 5.5–5.9 dB error versus 12.8–14.4 dB). Overall the existing diagnostic still favors the current synth: **10.66 dB current versus 20.15 dB new**, mean absolute smoothed spectral error across eight held-out notes. A secondary energy-weighted diagnostic also favors current (5.54 vs 11.18 dB). This additional diagnostic was introduced after seeing the first results, so it is exploratory.

Inspection verified plausible pitch peaks, but exposed strong register-dependent spectrum differences, especially the fundamental/second-partial balance around A3 and the absence of sub-fundamental attack/room energy in the tonal A5 model. Six-semitone parameter interpolation does not guarantee faithful voicing. The result is a reviewable overhaul prototype, **not an established sound improvement**. The existing live engine remains unchanged. Judge the musical example before investing in a finer parameter surface or a separately modeled transient/noise component (the 2025 follow-up is relevant there).

## Reproduce and verify

```sh
build/body-venv/bin/python tools/partial_audition.py
```

Requires the same NumPy/SciPy environment and Salamander dataset as the body experiment, plus its existing `build/body-fit/bodyrender` and held-out `.f32` renders. Run `tools/body_audition.py` first if those are missing. The current dynamic-library harness uses macOS `-dynamiclib`; the C kernel itself has no platform dependencies.

The generator checks finite renders at low/high notes and velocities and exact output equivalence across 257- versus 4096-sample processing blocks, including note-off inside a block. It records source/sample hashes and both spectral diagnostics. WAV validation checks sample count, peak headroom, and matched clip RMS. Website production build and TypeScript validation are run separately. Browser playback and optional WebMCP runtime tools have not been automatically tested.

Generated `patch.h` can be included by a host and passed to `pf_partial_init`; `pf_partial_process` adds into a zeroed or existing mix buffer. `score.json` is the reproducible musical event list. No changes to the existing live engine or its defaults have been made.

Reference audio and extracted calibration data derive from Salamander Grand Piano by Alexander Holm (CC BY 3.0). The audition page credits the source and modifications.

## Listening decision and Beethoven passage

The user confirmed a strong preference for the physics-informed model after blind listening to Small hours. Preserve this patch as the baseline for the next attack experiment.

`tools/beethoven_audition.py` adds a 23-second opening of Beethoven Sonata No. 1, Op. 2 No. 1, first movement. It reads the local (n)ASAP `Beethoven/Piano_Sonatas/1-1/KimG01.mid` in the segno corpus. The metadata identifies this as Sonata 1, movement 1; no corresponding MAESTRO audio is listed. Thus the third version remains an explicitly labeled sampled-piano reconstruction.

159 note events and 83 sustain-controller events precede the selected cutoff. MIDI tempo-map timing and velocities are preserved. CC64 is thresholded at 64; note-offs occurring under sustain are deferred until pedal-up. Admission stops at the first annotated downbeat after 20 seconds of excerpt time; pending notes release at that boundary and a tail is retained. All three paths use the same schedule and the existing 240 ms damper envelope. No patch fitting or synthesis-code change was made. The three 44.1 kHz mono WAVs have common headroom and whole-clip RMS mismatch below 0.000001 dB.

The script records source/patch hashes and the derived event list in `beethoven-score.json`. Performance-derived Beethoven excerpts are credited to ASAP/(n)ASAP, KimG01, and provided under CC BY-NC-SA 4.0. Sampled reference also credits Alexander Holm's Salamander Grand (CC BY 3.0). The earlier musical and body experiments and their local judgments are preserved.

To regenerate the current set, run `tools/partial_audition.py` first, then `tools/beethoven_audition.py` to restore the Beethoven entry and audio.
