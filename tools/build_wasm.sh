#!/bin/sh
# Build docs/pfsynth.wasm with wasi-sdk (build/wasi/wasi-sdk, see docs/README.md).
set -e
cd "$(dirname "$0")/.."
SDK=${WASI_SDK:-build/wasi/wasi-sdk}
test -x "$SDK/bin/clang" || { echo "wasi-sdk not found at $SDK (download from https://github.com/WebAssembly/wasi-sdk/releases)"; exit 1; }
"$SDK/bin/clang" --target=wasm32-wasip1 --sysroot="$SDK/share/wasi-sysroot" -O3 -std=c99 -Wall -Isrc -mexec-model=reactor \
  -Wl,--initial-memory=33554432 -Wl,--max-memory=268435456 \
  src/core/pf_partial.c src/core/pf_attack.c src/core/pf_resonance.c src/host/midi.c src/host/pfplayer.c src/host/pfwasm.c \
  -o docs/pfsynth.wasm
ls -la docs/pfsynth.wasm
