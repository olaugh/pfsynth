# Body response experiment 01

Status: audition candidate; **not promoted to the live synth**. The current body scored better on the chosen spectral diagnostic. Listening judgments are still needed; a successful filter fit is not evidence of realistic piano sound.

## What changes

`src/host/bodyrender.c` renders identical default FDTD string and hammer excitation into two paths: the raw string signal and the existing stereo feedback-delay body (downmixed). `src/core/pf_bodyfit.c` processes the raw signal with 32 parallel fixed-pole filters and a direct term. No live engine or default preset changes are required.

The global correction is fitted to Salamander C2, C3, C4, C5, C6 at sample layers 6 and 13 (synth velocities 48/127 and 100/127). A2–A6 at those two dynamics are held out. It follows Bank's fixed-pole parallel-filter approach, with our own numerator-fitting code. This is an output-spectrum correction, not an identified physical bridge admittance or soundboard measurement.

The target is optimized on 24 log-frequency knots with smoothness regularization and +/-24 dB bounds, using training-note spectra and accounting for per-note RMS normalization. A preliminary ratio estimator was replaced because it ignored weak source bands. No held-out note enters either fit. The final target is approximated by a minimum-phase parallel filter bank. Coefficients are quantized to float32; filter state is double. Coefficients apply only at 44.1 kHz.

## Result and limits

Mean absolute smoothed log-power error in dB, first 3 seconds, after first-2-second RMS normalization (lower is better):

| Notes | Current | Fitted |
|---|---:|---:|
| Training C notes | 10.27 | 16.52 |
| Held-out A notes | 10.52 | 17.52 |

These are diagnostic spectral errors, not perceptual quality scores. The objective used to design the correction differs from the final time-domain evaluation. The current feedback-delay network also changes temporal density; a smooth static correction cannot reproduce that behavior, create missing excitation energy, or correct string decay independently. The result does not establish that soundboard modeling is unimportant. It argues against replacing the current body with this particular fit without listening evidence.

Next decision: audition blind held-out notes, then inspect the largest excitation/partial-decay mismatches before investing in a more elaborate body model. A future measured body impulse response or coupled modal body could be a distinct experiment.

## Audition

Private listening page: https://pfsynth-body-audition.olaughlin.chatgpt.site

The `audition` application contains 20 single-note trials plus a held-out arpeggio. Each trial offers randomized A/B and a real-piano reference. All versions are onset-aligned, mono, 44.1 kHz; first-two-second RMS levels match, with a common peak-safe gain and short ending fades. Switching during playback crossfades synchronized buffers; restart compares attacks. There is no added room reverb or pedal model. The recorded reference retains its original room/body radiation.

Blind identities remain stable per clip on the device. Votes and notes are local browser storage; exported JSON includes identities for analysis. Preview and hosted origins have separate browser storage. The phrase uses identical excitation weights for the two synth paths, with independent reference-note normalization.

Reference excerpts: Salamander Grand Piano by Alexander Holm, CC BY 3.0, trimmed/downmixed/level-matched. See the reference dataset README in `calib` and the linked license in the page.

## Reproduce

From the synth repository root, with the existing Salamander dataset:

```sh
python3 -m venv build/body-venv
build/body-venv/bin/pip install numpy==2.5.2 scipy==1.18.1
build/body-venv/bin/python tools/body_audition.py
cd audition
npm ci
npm run dev
```

The generator currently uses the macOS C compiler's `-dynamiclib` option. Generated coefficients and diagnostics are in this directory; WAVs and the manifest are in `audition/public`. Source/sample hashes are recorded in `report.json`.

Validation: stable quantized poles (maximum radius 0.9993926), impulse-tail decay, C versus SciPy filter output (maximum discrepancy 1.44e-8), finite renders, 63 WAV files with no clipping and worst within-trial RMS mismatch 0.000021 dB, and unchanged string/body source hashes. The website has a production build and TypeScript check. Browser playback has not been automated. Optional WebMCP tools are feature-detected; no supported WebMCP validation context was available, so their runtime contract is unverified.
