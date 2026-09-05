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
  attack, mellow tail). Overall loop gain sets the fundamental T60, which is **pitch-
  dependent**: `T60(f0) = decay_t60·(f0/440)^decay_pitch`, `decay_pitch ≈ -0.6` — the bass
  rings many seconds, the treble dies in ~1–2 s, fit to a real grand.
- **Dispersion** — a cascade of first-order allpass filters producing stiff-string
  inharmonicity, partials stretched as `f_n ≈ n·f0·√(1 + B·n²)`. `B` is **pitch-dependent**,
  fit to a real grand: `B(f0) = inharmonicity·(f0/440)^inharm_pitch` with `inharm_pitch ≈
  1.5` — the treble is far stiffer/sharper than the bass. The allpass coefficient is fit at
  init to approximate the target stretch (one coefficient can't match all partials exactly;
  it also realizes ~1.8× the requested `B`, so the `inharmonicity` param is pre-divided to
  land the *output* B on the measured value — see Calibration).
- **Nonlinear felt hammer** — a mass with a hardening spring, force `F = K·δ^p` (compression
  `δ`, exponent `p ≈ 2–3`). The hammer–string interaction is a delay-free loop (force depends
  on string velocity depends on force); it's resolved per sample with an **implicit Newton
  step**. The hammer injects the excitation — this is the attack and the soul of the sound.
  **Velocity → brightness**: the felt stiffness `K` scales with strike velocity as
  `(vel/0.6)^hammer_vel_hardness`, so a fast strike meets a harder hammer (shorter contact →
  brighter, clangy ff) and a slow one a softer hammer (longer contact → mellow pp). This is
  the main dynamics→timbre coupling; without it, velocity only changes loudness. (The rich
  soundboard masks some of it in the mix — a trade against the body fullness.)
- **Graduated hammers + contact-noise bite** — the hammer is stiffer/lighter toward the
  treble (`hammer_pitch_k/m`) so the strike stays short relative to the period and excites a
  rich harmonic series at every pitch (a fixed hammer lowpasses high notes into dull sines).
  A smooth felt force still can't make the percussive *attack flash*, so `attack` adds a
  short broadband contact-noise burst on the radiated output at each onset, enveloped to the
  attack peak (the hammer/key "knock") — the difference between "piano" and "harp."
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
frequencies with deterministic jitter) added on top of a dry path, i.e. a parallel resonant
EQ that *colors* the string tone with the body. The modes are deliberately **short** (~10
ms): long modes ring *up* over ~60 ms and smear the percussive strike into a slow swell
(sounds wrong / "plucky"); short modes track the string so the attack stays sharp. The
sustained body / room comes from the reverb (below), not from the soundboard ringing.

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

### Room reverb (`pf_reverb`)
A real piano recording has a room around it; the soundboard alone is bone dry. `pf_reverb`
(core) is a compact Freeverb-style stereo reverb — 8 parallel damped feedback combs into 4
series allpasses per channel, the right side offset a few samples for stereo. It runs once
over the final mix (after the body), giving the sustained space/ambience the short-mode body
no longer provides. From constants (delay tables), so it ships tiny; the delay buffers are
runtime RAM. Live **Reverb** wet knob in the host (~0.3 default).

### Calibration against a real piano (`src/host/analyze.c`)
Rather than tune everything by ear, the model is fit to a real grand (the free **Salamander
Grand**, Yamaha C5, isolated notes × 16 velocities — `calib/`, gitignored). `make analyze`
builds `pfsynth-analyze <file.wav> <midi-note>`, a dependency-free tool that measures, per
note: the partial frequencies → inharmonicity `B` (via a `(f_k/k)²` vs `k²` regression, so
stretched tuning falls out), the fundamental's decay → `T60`, and the spectral centroid.
Sweeping the sample set gave `B ~ f0^1.5` and `T60 ~ f0^-0.6`, now baked into the model.
Close the loop by rendering a model note and running it back through `analyze`; the realized
`B`/`T60` are checked against the measured targets. (The single-coefficient dispersion only
*approximates* a given `B`, so the requested value is calibrated to land the realized one.)

**Register balance**: the samples also preserve real per-note loudness (they're not
peak-normalized). Measuring it showed the bare waveguide+body under-radiate the treble by
~20 dB vs a real grand — the top was nearly inaudible. `out_gain` now gets a per-register
makeup `(f0/110)^output_pitch` (≈0.8, capped ×10), fit so the model's A4–A6 peak balance
matches the Salamander (~0.7 of the bass).

### Deferred to later milestones
Sympathetic resonance, accurate dispersion (realize a target `B` exactly), velocity-
brightness vs the real samples, the rest of the instrument family, the serialized song format.

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
  transport, with a now-playing 88-key keyboard that lights up as the song sounds. On
  terminals with pixel graphics (kitty protocol / sixel, via notcurses `NCBLIT_PIXEL`) it's
  drawn as a real black-and-white keyboard bitmap; it falls back to an ASCII strip otherwise.
  **A/B reference** (`a` key): toggles the output between the synth and the *real maestro
  recording* of the loaded piece (MIDI and audio are sample-aligned in maestro). The audio is
  extracted on demand from `calib/maestro-full.zip` into `calib/maestro-ref/` and loaded via
  `wav_read_stereo`; the engine outputs it in place of the synth while the synth keeps
  rendering underneath, so toggling is seamless. Invaluable for matching the model to a real
  grand by ear.
  The MIDI files carry no titles, so the browser and a `/` search view read composer /
  title / duration from the dataset CSV (`maestro-v3.0.0.csv`), parsed once at startup
  and keyed by relative path.

```
make tui                         # build build/pfsynth-tui
./build/pfsynth-tui [lib-dir]    # default lib: maestro/midi/maestro-v3.0.0
```

TUI keys: TAB switch pane · up/dn select · left/right adjust param · `/` search by
composer/title · ENTER load file · SPACE play/pause · `[` `]` seek · `a` A/B synth vs
reference recording · ESC up a directory (quits at the library root / on the params pane,
and exits search) · `Q` quit (both via a confirmation modal).

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
make analyze   # build pfsynth-analyze (measure real piano samples to fit the model)
make clean
```

No external dependencies. The render harness in `src/host/main.c` is driven by a hardcoded
`{note, velocity, time_sec}` event list (foreshadowing the serialized song format) and
peak-normalizes the final buffer so output is always audible regardless of absolute model
scale. The hardcoded SONG is currently an octave arpeggio; it writes two stereo files for an
A/B: `out_dry.wav` (no reverb) and `out.wav` (+ room reverb); both have the sharp,
non-swelling body.

## Conventions

- Core code: ANSI-ish C, no platform headers, no allocation, double-precision filter state,
  `float` delay lines.
- Keep commits small.
- When tuning the physical model, verify by rendering and inspecting stats (peak, RMS,
  hammer contact duration, NaN check) — the harness prints these per note.

## Experiments in progress (2026-09)

The FDTD voice above is the *original* synth. Listening preferred a new **physics-informed partial
model** (`src/core/pf_partial.{h,c}`, fitted patch in `experiments/partial-piano/`, see its README),
and the current reference to imitate is **Pianoteq 6 "Steinway D Close Mic Classical"** rendered
as a black box (`tools/ptq_render.py`). Experiment 03 adds an onset component
(`src/core/pf_attack.{h,c}`: struck soundboard-mode bank + noise burst) and a Pianoteq-fitted
tonal patch — `experiments/attack-ptq/README.md`. Auditions live in `audition/` (own git repo).
The live engine/TUI still run the FDTD voice.
