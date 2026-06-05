# pfsynth - build.
# clang on macOS. Core stays portable; host is dev-machine-only.

CC      := clang
CSTD    := -std=c11
WARN    := -Wall -Wextra
CFLAGS  := $(CSTD) $(WARN) -Isrc -O2
LDLIBS  := -lm

# Offline render harness (dependency-free).
SRC     := src/core/pf_string.c src/core/pf_board.c src/host/wav.c src/host/main.c

# Interactive TUI: core + engine + midi + CoreAudio + notcurses.
TUI_SRC := src/core/pf_string.c src/core/pf_board.c src/host/midi.c \
           src/host/engine.c src/host/audio.c src/host/tui.c

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

.PHONY: all run debug tui analyze clean

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

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD) out.wav
