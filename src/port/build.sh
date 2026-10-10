#!/bin/sh
# rebuild cmr2deck. usage: ./build.sh [asan]
# note: the shared ~/.cache/zig got pruned and breaks links, so this pins a
# private global cache dir. do not remove that export.
set -e
. /home/deck/lena/toolchain/env.sh
cd "$(dirname "$0")"
export ZIG_GLOBAL_CACHE_DIR=/home/deck/lena/toolchain/zcache-local
SDLH=/home/deck/lena/.lena_cmr2/sdl3/SDL3-3.4.18/include
OUT=${OUT:-build/cmr2deck}
FLAGS="-O2 -g -Wall -Wextra -Wno-unused-parameter"
if [ "$1" = "asan" ]; then
  FLAGS="-O1 -g -fsanitize=address -fno-omit-frame-pointer"
  OUT=build/cmr2deck-asan
fi
mkdir -p build
zig cc -target x86_64-linux-gnu $FLAGS -o "$OUT" src/cmr2deck.c src/inflate.c \
  -I"$SDLH" -L/usr/lib64 -lSDL3 -lm -lpthread -ldl -Wl,--allow-shlib-undefined
echo "built $OUT"
