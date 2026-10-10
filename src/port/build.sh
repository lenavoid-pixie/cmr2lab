#!/bin/sh
# build.sh -- rebuild the CMR2 native viewer. usage: ./build.sh [asan]
#
# Needs: zig (or any clang with an x86_64-linux-gnu target), SDL3 headers,
# libSDL3, and the two SPIR-V shaders in shaders/ compiled to build/ (see
# README.md -- there is no glslc requirement at runtime, only the .spv files).
#
# Override any path from the environment:
#   SDLH=/path/to/SDL3/include   SDLLIB=/path/to/lib   OUT=build/cmr2deck
set -e
cd "$(dirname "$0")"

SDLH="${SDLH:-/usr/include/SDL3}"
SDLLIB="${SDLLIB:-/usr/lib64}"
CC="${CC:-zig cc}"
TARGET="${TARGET:--target x86_64-linux-gnu}"
OUT="${OUT:-build/cmr2deck}"
FLAGS="${FLAGS:--O2 -g -Wall -Wextra -Wno-unused-parameter}"

# zig's global cache is shared state; point it somewhere private if you have
# had "cannot open .../crt1.o" after a cache prune.
if [ -n "$ZIG_GLOBAL_CACHE_DIR" ]; then export ZIG_GLOBAL_CACHE_DIR; fi

if [ "$1" = "asan" ]; then
    # Known dead end on this toolchain: zig cc -fsanitize=address links and then
    # dies on `undefined symbol: __asan_unregister_elf_globals` (bundled
    # compiler-rt older than the generated instrumentation). Kept because it is
    # one line away from working on a toolchain where it does.
    FLAGS="-O1 -g -fsanitize=address -fno-omit-frame-pointer"
    OUT="build/cmr2deck-asan"
fi

if [ ! -f "$SDLH/SDL3/SDL.h" ] && [ ! -f "$SDLH/SDL.h" ]; then
    echo "build.sh: no SDL3 headers at SDLH=$SDLH -- set SDLH=..." >&2
    exit 2
fi

# sources live in ./src/ in the working tree and flat next to this script in
# the published repo -- accept both rather than have two copies of the build
if [ -f src/cmr2deck.c ]; then SRC="src/cmr2deck.c src/inflate.c"
else                           SRC="cmr2deck.c inflate.c"; fi

mkdir -p build
# shellcheck disable=SC2086
$CC $TARGET $FLAGS -o "$OUT" $SRC \
    -I"$SDLH" -L"$SDLLIB" -lSDL3 -lm -lpthread -ldl -Wl,--allow-shlib-undefined
echo "built $OUT"
