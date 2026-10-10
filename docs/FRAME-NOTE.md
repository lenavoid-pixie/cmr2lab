# FRAME-NOTE — what these pictures are, and what they are not

Written 2026-10-10 ~15:10 EEST, for Miami, because he asked for "a pretty thing to
look at". Every claim below is either a measurement in this repo, a line in the
decompilation, or it is labelled as mine. Read the labels — that is the point of
the file.

## The pictures

| file | what it is |
|---|---|
| `screenshots/cmr2deck-205a1N-1280x800.png` | one frame, 1280x800, MSAA 4x — the hero shot |
| `screenshots/cmr2deck-205a1N-4views-1280x800.png` | the same car from 4 yaws, 640x400 each, stacked |

Both come from `src/port/cmr2deck.c` — **the native viewer**, built on this Deck,
rendering on the Deck's own Vulkan device (RADV VANGOGH).

## 1. WHAT THIS IS

* It is the **game's own data**: geometry read out of `205a1N.c3d` (gzip → `CMPR`
  → `PP_F` payload) and textures out of `205a1.bfl` (27 textures, DXT5 DDS and
  TGA, all decoded by the viewer).
* It is a **car**: 14 parts, 1204 vertices, 888 triangles, 4 wheels placed from
  the file's own scene nodes, lit, textured, at 1280x800.
* It is the closest thing to "the game rendering" that exists on this Deck today:
  the *data* path is the game's, the *draw code* is ours.

## 2. WHAT THIS IS NOT — read this before you feel good about it

* **It is not the port rendering the game.** The decompiled game
  (`work/CMR2/outres`, the native binary) still boots only to its **frontend** and
  draws 2D there. No race has been started, no stage loaded, no car drawn by the
  game's own 3D path. This picture is **not** evidence that the port works.
* **It is not the M3 RHI.** `port/cmr2_rhi.h` still has no backend; 277 of the
  298 D3D7 call sites are render-state calls and the state tracker that would
  serve them does not exist.
* **There is no stage, no sky, no road, no opponent, no HUD, no physics.** The car
  floats in a studio backdrop that I drew.
* **No human has looked at this frame.** Miami will be the first. Everything I
  know about it is numbers and ASCII maps (`tools/preview.py`), and that is
  exactly why the note is this long.

## 3. WHAT CAME FROM THE GAME'S OWN CODE (with the address)

| in the frame | where it comes from |
|---|---|
| vertex layout: position 3f, normal 3f, colour dword, specular dword, uv0 2f, uv1 2f = 48 B | the loader's arithmetic, `Sector_RelocateStageMeshFile` (0x004b93c0), reconciled byte-exact against 205a1N |
| 888 triangles from 888 face records of 76 B, indices at +0x40 | same; verified by the closed counts, not by eye |
| wheels on the car's own four corners | `SceneNode` local `FixMatrix` at +0x58, 16.16 fixed, words 0,1,2 / 4,5,6 / 8,9,10 (the w word sits *between* the vectors) |
| **which texture each triangle uses** | the int32 at `MeshTriangle + 4 + field_0x2c * 4` with `field_0x2c = 0` — `Game_DrawMeshTextureRuns` (0x0049c510), `Graphics_DrawMeshTextureBatches` (0x0049c680) |
| **one draw per texture change** (27 runs) | same two functions: they walk the triangle list in order and start a new draw whenever the texture changes |
| **alpha blending on glass and lights, not on paint** | `Graphics_DrawMeshLOD` (0x0049c940) → `Graphics_SwitchAlphaBlendAndTest((pLod->flags >> 3) & 1)`; Mesh::flags is at +0x30, and 07SemiTrans (0x0000265c) and 14LightEffe (0x0000207c) are the two parts with bit 3 set |
| the blend pair `SRCALPHA / INVSRCALPHA` | (5,6) — `Graphics.cpp:6774`, `Sprite.cpp:561` |
| alpha test on, `ALPHAREF` 0x80 while blending is off and 1 while it is on, `ALPHAFUNC = GREATER` | `Graphics_SwitchAlphaBlendAndTest` + the device init at `Graphics.cpp:1888-1902` |
| no gamma correction anywhere | D3D7 into a plain 32-bit surface; the viewer targets `R8G8B8A8_UNORM` for the same reason |
| culling direction | **measured, not guessed**: `CULL=front` drops the car's mean luminance to 28.8 against 68.9 for `CULL=back`, and `CULL=back` ≈ `CULL=none` — so the faces being kept are the outside ones |

Two of these are the reason this frame looks like a car at all, and both were
wrong this morning:

1. **The texture was a guess.** The viewer matched part *names* against the
   texture table ("whl"→"whd", "gl"→"gli"), which sent every body part to
   texture 0 — `AP5NWBDf`, a nearly black texture (mean RGB 17,17,19). That is
   why the car rendered as a grey blob. The file says the body is texture 2
   (`ap5dbodf`, 1024x1024, and it holds the livery). It is now read, not guessed:
   **8 distinct textures bound across 27 runs, 0 runs refused.**
2. **Everything was drawn opaque.** The glass texture is dark and mostly
   transparent; drawn opaque it blacked out the windows. With the game's own
   alpha test and blend pair the glass and the light pods read correctly.

## 4. WHAT I FAKED, STUBBED, OR CHOSE — mine, not the game's

* **The lights.** The ambient value (0.40), the key light direction and gain
  (`-0.42, 0.72, 0.55` / 0.55) and the fill light (0.36, and the fill uses a
  half-lambert *wrap* rather than D3D7's straight lambert). CMR2's object light
  (`Graphics_SetLightingMode` mode 2 → `LightEnable(0)` with `g_sceneAmbientD3D`)
  is a **stage** light, and there is no stage here. If you want the honest
  version of this, the ambient should come from the game's own
  `g_sceneAmbientColour`, which is set per stage.
* **The ground shadow.** A planar projection of the car's own geometry onto
  y = 0 along the key light — same index buffer, flattened MVP, blended
  `dst * (1 - alpha)`. CMR2 has its own shadow-mesh system; this is not it.
* **The backdrop.** A gradient I wrote, drawn as a full-screen triangle. CMR2 has
  no garage; its cars are drawn inside a stage against sky, trees and road.
* **MSAA 4x.** The game rendered into a plain surface with no multisampling.
* **The camera.** Fixed by me: yaw 38°, elevation 14°, distance 1.30 × the car's
  longest side. The interactive viewer has a gamepad camera.
* **The material.** White, i.e. `g_sceneMaterial` as `Scene_RestoreLights`
  (0x004b2e50) sets it. Not stubbed wrongly — just noting it is a constant here.
* **Not implemented at all:** fog, the second uv set, the per-triangle
  texture-*address* clamp for material groups 0x70/0x71/0x73/0x74 (none of those
  groups occur on this car), specular (the game sets `D3DRS_SPECULARENABLE = 0`),
  vertex colours (`Graphics_DrawMeshLOD` uses lighting mode 2, which sets
  `D3DRS_COLORVERTEX = FALSE` — the viewer has a `VSHADE=1` switch to see that
  path, off by default), and the per-triangle texture **slot** selection.

On that last one, because it is the sharpest limit in this file: a triangle
record holds **ten** ints from +4 onward, not four. The file's own `field_0x2c`
is 0 everywhere, so slot 0 is what is bound here, and slot 0 is the right set on
every part of this car by name (body→`ap5dbodf`, wheels→`AP5NWhDf`,
interior→`ap5intdf`, glass→`ap5glidf`, lights→`ap5ligbr`, underside→`ap5unddf`).
But the game *does* move that field at runtime — `Car_UpdateWheelMeshStates`
(0x0042be30) sets it per wheel from the tyre state, values 0,3,4,5,6,7,8,9. So a
car mid-stage can bind a different slot than this frame does. This frame is the
car's clean, highest-detail configuration, not the only one.

## 5. THE BIGGEST REMAINING GAP

**The port does not draw any of this.** Everything above is the target image and
the specification for M3: a state tracker that can service
`Graphics_DrawMeshLOD`'s sequence (cull mode, lighting mode 2/3, alpha blend +
test, texture stage change, `DrawIndexedPrimitiveVB` per texture run) and a
fixed-function T&L path for the 21 draw call sites. The run split, the alpha
flags, the texture IDs and the node transforms are now all read from the file and
printed — so when the RHI lands, this frame is what it has to reproduce, and
`tools/preview.py --diff` is how we will check it.

Second gap, smaller and duller: this is **one car**. The per-triangle texture
path has been run over 205a1N only; the earlier corpus passes (259 cars) were
about counts and crashes, not about which texture each triangle names.

## 6. REPRODUCE IT

```sh
cd src/port
./build.sh shaders && SDLH=<SDL3 include> ./build.sh
Xvfb-less, on the Deck itself:
OFFW=1280 OFFH=800 ./build/cmr2deck 205a1N --game <install> --shot /tmp/hero.bmp
ffmpeg -i /tmp/hero.bmp screenshots/cmr2deck-205a1N-1280x800.png
```

Knobs that change the picture, all printed by the binary at startup:
`AMB`, `GAIN`, `FILL`, `LDIR`, `LDIR2`, `SHADOW=0`, `BG=0`, `MSAA=1|2|4|8`,
`CULL=none|back|front`, `VSHADE=1`, `NOCAR=1`, `YAW`, `ELEV`, `DIST`, `SPIN=1`,
`FULLSCREEN=`, `EXITFRAMES=n`, `TEXDUMP=DIR`.

`NOCAR=1` exists for honesty: it renders the backdrop and nothing else, so
"how much of the frame is the car" is a measured number (21.5%) instead of an
impression.
