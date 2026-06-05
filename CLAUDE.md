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

Digital-waveguide piano as the flagship voice. The current voice (`pf_string`) is a small
bank of coupled dispersive, lossy waveguide loops (the 2–3 unison strings of a piano note)
sharing one nonlinear felt hammer. Each loop is:

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
  **Velocity → brightness**: the felt stiffness `K` scales with strike velocity as
  `(vel/0.6)^hammer_vel_hardness`, so a fast strike meets a harder hammer (shorter contact →
  brighter, clangy ff) and a slow one a softer hammer (longer contact → mellow pp). This is
  the main dynamics→timbre coupling; without it, velocity only changes loudness. (The rich
  soundboard masks some of it in the mix — a trade against the body fullness.)
- **Strike-point comb** — the hammer hits at `strike_pos` (a fraction of the string length,
  ~1/8), so partials with a node there can't be excited. Modeled as the injected excitation
  being high-passed by `(1 − z^−β)`, `β = strike_pos·period`: the delayed term is the wave
  reflecting off the near end back to the hammer (which feels it, so the comb falls out of
  the physics, not a post-EQ). Notches partials `n = 1/strike_pos, 2/strike_pos, …` and
  boosts the low-mid partials between — the characteristic woody piano shape. `0` = off.

The voice also has a **damper**: `pf_string_release()` switches the loop to a fast
`release_t60` decay, modeling the felt damper falling on note-off. A held sustain pedal
just means the host doesn't call it.

### Coupled strings — beating + two-stage decay
A single loop decays as a pure exponential, which sounds electronic. A real note has 2–3
unison strings, slightly detuned, coupled at the bridge. `pf_string` models that:
`unison_strings` loops (`unison_detune` cents apart) feed a shared bridge each sample. The
bridge applies a load `in_k = b_k − μ·Σb_j` (`μ` = `coupling`): the **common mode** (all
strings in phase, large `Σ`) loses energy fast → the bright "prompt" attack; the
**differential mode** (out of phase, `Σ≈0`) is nearly lossless → the slow singing
"aftersound." The cent-scale detune makes the loops beat and bleeds prompt energy into the
aftersound. `unison_strings = 1` recovers the old single-loop voice (and disables coupling).
Tuned to `μ ≈ 0.006` — the loss is per-bridge-pass, so it scales with pitch (treble notes
get a faster prompt, like a real piano).

### Soundboard (`pf_board`) — the body resonance
A bare string is a thin, synthetic-sounding source; in a real piano you mostly hear the
**soundboard** the strings drive. `pf_board` (core) models that body as a parallel bank of
~40 second-order modal resonators (a coded modal impulse response — log-spaced mode
frequencies with deterministic jitter, low modes ringing longer than high) added on top of
a dry path, i.e. a parallel resonant EQ that *colors* the string tone with the body.

This is **commuted synthesis** done the cheap way: after the hammer the instrument is ~LTI
and there is exactly one shared soundboard, so filtering the summed mix of all voices
through one body is mathematically identical to convolving each note's excitation with the
body — but costs one filter bank, not one per voice. The host runs a single `pf_board` over
the final mix (`pf_engine` for live playback, the offline harness for `out.wav`). The whole
body is generated from a few `pf_board_params` constants — no sampled IR (which would be
incompressible) — so it stays tiny. The host exposes a live **Body** mix knob; `mix = 0`
bypasses it to A/B against the bare string.

**Stereo body** (`pf_board_stereo`): the bank feeds a different Schroeder-allpass chain per
channel. Each chain is allpass (flat magnitude), so L and R keep the body's exact spectrum
and energy — the image stays perfectly balanced for any signal — while the different delays
give a different phase per side, decorrelating the body down into the low-mids (a plain
one-pole allpass only spreads the highs). The dry path stays centered, so bass is solid and
centered while the mid/treble body opens up — `stereo_width` (~0.2) sets the amount. This is
what the host actually runs over the mix; the offline harness writes a stereo `out.wav`.

### Deferred to later milestones
Sympathetic resonance, per-register decay tuning, the rest of the instrument family, the
serialized song format.

## Host tooling

Beyond the offline WAV renderer there's an interactive **notcurses TUI** for auditioning
patches and playing back MIDI. It is strictly dev-machine tooling — none of it ships.

- `src/host/midi.{h,c}` — Standard MIDI File loader (format 0/1). Merges tracks, applies
  the tempo map, and flattens to a timed event stream (note on/off + CC64 sustain). Used
  to play the maestro dataset.
- `src/host/engine.{h,c}` — polyphonic piano engine. Voice pool with stealing, a per-MIDI-
  note bank of pre-initialized voice templates (so note-on is a memcpy + strike, never an
  init on the audio thread), sustain-pedal handling, and a **sample-accurate sequencer**.
  One mutex guards the whole engine across the UI/audio thread split (fine for a dev tool).
  Output is the core's tiny displacement scaled by a makeup gain + a `tanh` safety clip.
- `src/host/audio.{h,c}` — CoreAudio default-output AudioUnit (no external deps; system
  frameworks only). Pulls `pf_engine_render` from the RT thread, 44.1k float stereo.
- `src/host/tui.c` — the notcurses UI: live patch editor, maestro file browser, and
  transport, with a now-playing strip showing the 88 keys light up as the song sounds.
  The MIDI files carry no titles, so the browser and a `/` search view read composer /
  title / duration from the dataset CSV (`maestro-v3.0.0.csv`), parsed once at startup
  and keyed by relative path.

```
make tui                         # build build/pfsynth-tui
./build/pfsynth-tui [lib-dir]    # default lib: maestro/midi/maestro-v3.0.0
```

TUI keys: TAB switch pane · up/dn select · left/right adjust param · `/` search by
composer/title · ENTER load file · SPACE play/pause · `[` `]` seek · ESC exit search ·
`Q` quit.

The TUI depends on **notcurses** (homebrew keg at `/opt/homebrew/opt/notcurses`, no
pkg-config installed so the path is hardcoded in the Makefile) and macOS CoreAudio. The
offline `pfsynth` target stays dependency-free.

## Core API (`src/core/pf_string.h`)

```c
pf_string_defaults(&params, sample_rate);  // sensible piano defaults
pf_string_init(&voice, &params, f0_hz);    // build one voice at a pitch
pf_string_strike(&voice, velocity);        // velocity in (0,1] -> hammer launch
pf_string_process(&voice, out, n);         // ADDITIVE mix of n samples into out[]
```

`pf_string_process` is **additive** — chords/polyphony are just multiple voices summed into
the same buffer. The host zeroes the buffer first.

The shared body filter (`src/core/pf_board.h`) sits at the output, run once over the mix:

```c
pf_board_defaults(&bp, sample_rate);       // sensible piano-body defaults
pf_board_init(&board, &bp, sample_rate);   // build the modal bank
y = pf_board_tick(&board, x);              // one sample (or pf_board_process over a block)
pf_board_reset(&board);                    // clear ring (on load/seek/panic)
```

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
scale. The hardcoded SONG is currently a velocity staircase; it writes two stereo files for
an A/B: `out_flat.wav` (velocity→brightness off — loudness only) and `out.wav` (on).

## Conventions

- Core code: ANSI-ish C, no platform headers, no allocation, double-precision filter state,
  `float` delay lines.
- Keep commits small.
- When tuning the physical model, verify by rendering and inspecting stats (peak, RMS,
  hammer contact duration, NaN check) — the harness prints these per note.
