# cmr2lab viewer — first real port code

Real SDL2 C. Camera (orbit + free), theater timeline (pause/step/speed),
software renderer, headless-capable via `SDL_VIDEODRIVER=dummy`.

## Build
    apt-get install -y libsdl2-dev
    gcc -O2 -o viewer viewer.c $(pkg-config --cflags --libs sdl2) -lm

## Run
    ./viewer <cloud.bin> <out.ppm> [w] [h] [ax] [ay] [az]

`cloud.bin` is the c14 block of a car as raw little-endian floats
(1204 records x 12 floats = 57,792 bytes).

    # to make cloud.bin from a .c3d (gzip + PP_F), see tools/c3d.py

## Status — honest
- compiles, runs, loads real CMR2 data, draws 1204 points   ✓
- theater controls present in the timeline struct            ✓
- headless render works                                      ✓
- WHICH THREE FIELDS ARE THE CAR: NOT YET KNOWN
  All nine combinations tried render as point clouds; none is
  confirmed to be car geometry. The label space is 220 combos
  and none has been visually confirmed yet.
