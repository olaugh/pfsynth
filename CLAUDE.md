# pfsynth

A physical-modeling software synthesizer. End goal: generate the music for a **64k PC
intro** targeting **Revision 2027** (~late March 2027).

## The one constraint that drives every decision

In a 64k intro the scarce resource is **compressed bytes of data**, not CPU:

- Samples are hopeless — incompressible waveform data.
- Physical modeling is the opposite: the instrument is **code** (a fixed, one-time cost)
  and a song is just a tiny event stream plus a few patch floats.
- Intros render the whole tune to a PCM buffer during the precalc/loading screen, so we
  are **not real-time**. CPU is effectively free.

**Therefore: optimize for small, compressible code and data. Do NOT optimize for runtime
speed.** Prefer generating constants in code over storing float tables. Prefer compact
integer event streams. A transcendental call per sample in the audio path is fine — it
runs once at precalc and costs zero bytes.

## Architecture — keep this split clean from day one

- `src/core/` — the **synthesis kernel**. Pure, portable C. No external deps beyond
  `<math.h>`/`<string.h>`. **No dynamic allocation, ever** (structs are fully defined in
  the headers so the host can stack/static-allocate them). This is what eventually ships
  inside the intro as the replayer, so code size and compressibility are first-class
  concerns. Must stay platform-agnostic — the eventual shipping target is Windows PE x64,
  but it must never know that.
- `src/host/` — **offline tooling**: WAV writing, CLI, the render harness. Runs only on
  the dev machine (macOS/clang for now). Unlimited size, free to use libc/libm/whatever.
  **Never let host concerns leak into core.**

## Synthesis approach

Digital-waveguide piano as the flagship voice. The current voice (`pf_string`) is a single
dispersive, lossy waveguide loop excited by a nonlinear felt hammer:

- **Delay line** — the waveguide loop, tuned to the note pitch (integer delay + a
  first-order allpass fractional tuner).
- **Loss filter** — a one-pole lowpass in the loop for frequency-dependent decay (bright
  attack, mellow tail). Overall loop gain sets the fundamental T60.
- **Dispersion** — a cascade of first-order allpass filters producing stiff-string
  inharmonicity, partials stretched as `f_n ≈ n·f0·√(1 + B·n²)`. `B` is a parameter; it's
  what makes it read as "piano" rather than "Karplus-Strong pluck." The allpass coefficient
  is fit at init to approximate the target stretch (one coefficient can't match all
  partials exactly — this is intentionally approximate for now).
- **Nonlinear felt hammer** — a mass with a hardening spring, force `F = K·δ^p` (compression
  `δ`, exponent `p ≈ 2–3`). The hammer–string interaction is a delay-free loop (force depends
  on string velocity depends on force); it's resolved per sample with an **implicit Newton
  step**. The hammer injects the excitation — this is the attack and the soul of the sound.

### Deferred to later milestones
Multi-string coupling (beating / double decay), commuted soundboard, the rest of the
instrument family, the serialized song format.

## Core API (`src/core/pf_string.h`)

```c
pf_string_defaults(&params, sample_rate);  // sensible piano defaults
pf_string_init(&voice, &params, f0_hz);    // build one voice at a pitch
pf_string_strike(&voice, velocity);        // velocity in (0,1] -> hammer launch
pf_string_process(&voice, out, n);         // ADDITIVE mix of n samples into out[]
```

`pf_string_process` is **additive** — chords/polyphony are just multiple voices summed into
the same buffer. The host zeroes the buffer first.

## Build

```
make           # optimized build  -> build/pfsynth, renders out.wav
make debug     # -fsanitize=address,undefined
make run       # build + render
make clean
```

No external dependencies. The render harness in `src/host/main.c` is driven by a hardcoded
`{note, velocity, time_sec}` event list (foreshadowing the serialized song format) and
peak-normalizes the final buffer so output is always audible regardless of absolute model
scale.

## Conventions

- Core code: ANSI-ish C, no platform headers, no allocation, double-precision filter state,
  `float` delay lines.
- Keep commits small.
- When tuning the physical model, verify by rendering and inspecting stats (peak, RMS,
  hammer contact duration, NaN check) — the harness prints these per note.
