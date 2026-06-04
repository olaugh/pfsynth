# pfsynth - offline render harness build.
# clang on macOS. Core stays portable; host is dev-machine-only.

CC      := clang
CSTD    := -std=c11
WARN    := -Wall -Wextra
CFLAGS  := $(CSTD) $(WARN) -Isrc -O2
LDLIBS  := -lm

SRC     := src/core/pf_string.c src/host/wav.c src/host/main.c
BUILD   := build
BIN     := $(BUILD)/pfsynth
DBG     := $(BUILD)/pfsynth-debug

.PHONY: all run debug clean

all: $(BIN)

$(BIN): $(SRC) | $(BUILD)
	$(CC) $(CFLAGS) $(SRC) -o $@ $(LDLIBS)

# Address + UB sanitizers for catching memory / numeric trouble.
$(DBG): $(SRC) | $(BUILD)
	$(CC) $(CSTD) $(WARN) -Isrc -O1 -g -fsanitize=address,undefined \
		$(SRC) -o $@ $(LDLIBS)

debug: $(DBG)
	./$(DBG)

run: $(BIN)
	./$(BIN)

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD) out.wav
