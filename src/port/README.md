# `cmr2deck` — the native viewer

The first real code of the native port: a C program that reads a `.c3d` model out
of your own copy of Colin McRae Rally 2.0, reads the matching `.bfl` texture
container, and draws the car with SDL3 on a Vulkan device.

It is **one car, spinning**. No tracks, no physics, no HUD, no menus. It is the
proof that the format reader and the renderer work, and it is the backend the
decompilation's D3D7 layer is meant to link against.

## Build

```sh
SDLH=/usr/include/SDL3 SDLLIB=/usr/lib64 ./build.sh
```

Needs a clang with an `x86_64-linux-gnu` target (`zig cc` is what it is tested
with), SDL3 3.4.x headers, `libSDL3`, and two SPIR-V shaders **next to the
binary**:

```sh
mkdir -p build
glslc shaders/car.vert -o build/car.vert.spv
glslc shaders/car.frag -o build/car.frag.spv
```

There is no shader compiler in the build step — the `.spv` files are loaded at
runtime — so if you have no `glslc`, any SPIR-V for the two stages in
`shaders/` will do. `build.sh asan` exists and is a **known dead end** on the
toolchain this was developed on (see `docs/PORT-PLAN.md` §6).

## Run

```sh
./build/cmr2deck 205a1N --game /path/to/your/game/install --shot /tmp/car.bmp
./build/cmr2deck --list --game /path/to/your/game/install
```

Arguments: `[car] --game DIR --shot FILE --list --mesh indexed|strip`.

Environment knobs, all optional: `CMR2_GAME`, `MESH=indexed|strip`,
`YAW` / `ELEV` / `DIST` (fixed camera, for reproducible screenshots), `WX`/`WY`/`WZ`,
`NOWHEELPLACE=1`, `SPIN=1`, `FULLSCREEN=0`, `EXITFRAMES=n`.

With `--shot` it renders offscreen and exits — no window, nothing on your
desktop. Without it, it opens one.

## What it prints, and why

Every run states its own evidence, so a wrong render cannot pass quietly:

```
[MESH] indexed parts=14 verts=1204 tris=888  (vcap=1204/1212 icap=2664/3620)
[MESH] degenerate tris=0  holes/empty parts=0  bridging = 0.00% ...
[OK] geometry: 14 parts, 1204 verts, 2664 indices (888 of 888 face records kept = 100.0%)
[INFO] bbox x[-1.909,1.908] y[-0.557,0.723] z[-0.888,0.888] = 3.82 x 1.28 x 1.78 m
[OK] textures: 26 decoded from the .bfl, 1 placeholders
```

`MESH=strip --mesh strip` runs the *rejected* topology through the same path as
a control. It prints `1176 of 888 face records kept = 132.4%` — more triangles
than the file contains. That is what settled the topology question, and it is
left in as a live control rather than a footnote.

## Status — honest

* Loads and renders a car from the retail files; exits 0. Verified on **6 of the
  259 cars**, not all of them. Four give a car-length bbox; two load interior
  part lists and give a cockpit-sized one.
* **Textures load for some cars and not others.** Of the 220 car containers,
  **44 hold `.dds` and 176 hold `.tga`**; the TOC scorer in this file only
  validates `.dds` blocks, so the TGA containers are rejected whole and those
  cars draw flat white. Root cause, evidence and the fix: `docs/PORT-PLAN.md` §4.
* No sanitizer builds on the development toolchain, so **every correctness claim
  here is empirical**. That is a real gap: a 12-byte vertex-base error survived a
  plausible-looking render for a long time.

## Legal

No game data is in this repository and none is needed to build it. The reader
opens files from a copy of the game **you** own, at runtime. See the repository
README.
