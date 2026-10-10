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

## What it draws now (2026-10-10, 15:00)

The viewer reads the texture of every triangle from the file
(`MeshTriangle + 4`, `field_0x2c = 0`), splits the triangle list into runs the way
`Game_DrawMeshTextureRuns` does, and draws one indexed run per texture with the
game's own alpha test (ALPHAREF 0x80 / 1, D3DCMP_GREATER) and blend pair
(SRCALPHA / INVSRCALPHA). Glass and light pods blend; paint does not.

```
[TEXRUNS] 27 runs from the file's own per-triangle texture IDs, 8 distinct textures bound
[TEXRUNS]  0:AP5NWBDf x48  2:ap5dbodf x496  4:AP5NWhDf x144  12:ap5intdf x56
           13:ap5glidf x44  16:ap5glodf x56  22:ap5unddf x26  24:ap5ligbr x18
[OK] 24 shadow draws, 27 texture-run draws (0 runs refused: no texture), 563792/1024000 pixels lit (55.1%)
```

The frame this produces is `../../screenshots/cmr2deck-205a1N-1280x800.png`, and
**`../../docs/FRAME-NOTE.md` says exactly what is the game's and what is mine.**
Read that before quoting any of this as "the port renders the game" — it does not.
It renders the game's *data*, with our draw code, outside the game.

The lights, the ground shadow, the backdrop, the MSAA and the camera are mine and
are printed at startup:

```
[CFG] samples=4 cull=back ambient=0.40 key=0.55 fill=0.36 light=(-0.42,0.72,0.55)
      fill_dir=(0.68,0.31,-0.66) vshade=0 backdrop=1 castshadow=1
```

## Measurement, not eyeballing

`tools/preview.py` prints a luminance map, a hue map and a colour histogram for a
frame; `--diff BACKDROP FRAME` measures only the pixels the car covers, against a
frame rendered with `NOCAR=1`. That is how "the car is 21.5% of the frame" and
"`CULL=front` drops the car's mean luminance to 28.8" are known.
`tools/car_materials.py` prints the per-part vertex colours, specular words and
non-zero mesh-record fields straight out of a `.c3d`.
