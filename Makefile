# pfsynth - build.
# clang on macOS. Core stays portable; host is dev-machine-only.

CC      := clang
CSTD    := -std=c11
WARN    := -Wall -Wextra
CFLAGS  := $(CSTD) $(WARN) -Isrc -O2
LDLIBS  := -lm

# Offline render harness (dependency-free).
SRC     := src/core/pf_string.c src/core/pf_board.c src/core/pf_reverb.c \
           src/host/wav.c src/host/main.c

# Interactive TUI: core + engine + midi + CoreAudio + notcurses.
TUI_SRC := src/core/pf_string.c src/core/pf_board.c src/core/pf_reverb.c \
           src/core/pf_symp.c src/host/midi.c src/host/engine.c src/host/audio.c \
           src/host/au_host.c src/host/wav.c src/host/tui.c

# notcurses lives in its homebrew keg (no pkg-config installed).
NC_PREFIX := /opt/homebrew/opt/notcurses
NC_CFLAGS := -I$(NC_PREFIX)/include
NC_LIBS   := -L$(NC_PREFIX)/lib -lnotcurses -lnotcurses-core
FRAMEWORKS := -framework AudioUnit -framework AudioToolbox \
              -framework CoreAudio -framework CoreFoundation

BUILD   := build
BIN     := $(BUILD)/pfsynth
DBG     := $(BUILD)/pfsynth-debug
TUI     := $(BUILD)/pfsynth-tui
ANALYZE := $(BUILD)/pfsynth-analyze

.PHONY: all run debug tui analyze midirender ltas fdtdtest pfmatch specdiff decay clean

all: $(BIN) $(TUI)

$(BIN): $(SRC) | $(BUILD)
	$(CC) $(CFLAGS) $(SRC) -o $@ $(LDLIBS)

# Address + UB sanitizers for catching memory / numeric trouble.
$(DBG): $(SRC) | $(BUILD)
	$(CC) $(CSTD) $(WARN) -Isrc -O1 -g -fsanitize=address,undefined \
		$(SRC) -o $@ $(LDLIBS)

$(TUI): $(TUI_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $(NC_CFLAGS) -Wno-deprecated-declarations \
		$(TUI_SRC) -o $@ $(LDLIBS) $(NC_LIBS) $(FRAMEWORKS)

debug: $(DBG)
	./$(DBG)

run: $(BIN)
	./$(BIN)

tui: $(TUI)
	@echo "run with: ./$(TUI) [midi-library-dir]"

# Offline analysis tool: measure real piano samples to fit the model.
$(ANALYZE): src/host/analyze.c | $(BUILD)
	$(CC) $(CFLAGS) src/host/analyze.c -o $@ $(LDLIBS)

analyze: $(ANALYZE)
	@echo "run with: ./$(ANALYZE) <file.wav> <midi-note>"

# Offline MIDI -> WAV through the full engine (reproduces live playback exactly).
MR_SRC := src/core/pf_string.c src/core/pf_board.c src/core/pf_reverb.c \
          src/core/pf_symp.c src/host/midi.c src/host/engine.c src/host/au_host.c \
          src/host/wav.c src/host/midirender.c
midirender: $(MR_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $(MR_SRC) -o $(BUILD)/pfsynth-midirender $(LDLIBS) $(FRAMEWORKS)
	@echo "run with: ./$(BUILD)/pfsynth-midirender <file.mid> <out.wav> [secs] [master]"

# Calibration / fidelity tooling (fit the model to real recordings).
#   ltas     - long-term-average-spectrum diff of two WAVs (synth vs reference)
#   fdtdtest - one isolated note: partials, contact time, stability, decay envelope
#   pfmatch  - optimize string/hammer params to match isolated Salamander notes
#              (multi-resolution STFT + decay-envelope loss, random-restart search)
ltas: src/host/ltas.c src/host/wav.c | $(BUILD)
	$(CC) $(CFLAGS) src/host/ltas.c src/host/wav.c -o $(BUILD)/ltas $(LDLIBS)
	@echo "run with: ./$(BUILD)/ltas <a.wav> <b.wav>"

fdtdtest: src/core/pf_string.c src/host/fdtdtest.c | $(BUILD)
	$(CC) $(CFLAGS) src/core/pf_string.c src/host/fdtdtest.c -o $(BUILD)/fdtdtest $(LDLIBS)
	@echo "run with: ./$(BUILD)/fdtdtest <midi-note> <velocity>   (env: REL SKIP ENV COUP DET US HM HK HE PM PK)"

PM_SRC := src/core/pf_string.c src/core/pf_board.c src/host/wav.c src/host/pfmatch.c
pfmatch: $(PM_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $(PM_SRC) -o $(BUILD)/pfmatch $(LDLIBS)
	@echo "run with: ./$(BUILD)/pfmatch [iterations]   (or 'dump' for isolated-note A/B wavs)"

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD) out.wav

# Time-aligned spectrogram diff (synth vs a real recording of the same performance):
# error by register + attack/sustain. Diagnoses WHERE the model diverges.
specdiff: src/host/specdiff.c src/host/wav.c | $(BUILD)
	$(CC) $(CFLAGS) src/host/specdiff.c src/host/wav.c -o $(BUILD)/specdiff $(LDLIBS)
	@echo "run with: ./$(BUILD)/specdiff <synth.wav> <ref.wav>"

# Per-band decay-rate (T60) analysis - NON-FFT time-domain filterbank. Diagnoses
# "tinkly"/over-sustained treble that an FFT magnitude/LTAS comparison can't see.
decay: src/host/decay.c src/host/wav.c | $(BUILD)
	$(CC) $(CFLAGS) src/host/decay.c src/host/wav.c -o $(BUILD)/decay $(LDLIBS)
	@echo "run with: ./$(BUILD)/decay <a.wav> <b.wav>"

# demo-app support: portable player + CLI renderer (partial model + onset + pedal model)
pfrender: src/core/pf_partial.c src/core/pf_attack.c src/host/midi.c src/host/pfplayer.c src/host/pfrender.c
	$(CC) -O2 -std=c99 -Wall -o build/pfrender $^ -lm
app:
	./app/make_app.sh
