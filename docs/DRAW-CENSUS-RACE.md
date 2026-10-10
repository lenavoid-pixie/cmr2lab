# WHAT THE RHI IS ASKED TO DRAW IN A RACE — AND WHAT IT DROPS

`work/RACE`, 2026-10-11 ~02:1x EEST. Round: **the in-race census vs the frontend
census**. Every number here is measured on this Deck, in the game's own binary,
tonight. Where a number disagrees with anything written before it, this file wins
and says so.

Read with `STAGE-REACHED.md` (the handoff) and `CHECKPOINT.md` (how to re-run).
The question this round was handed:

> Now that the race loop is running: **what is the RHI being asked to draw during
> it, and what is it dropping?**

Answers, in the order they were measured. Three hypotheses died on the way and
they are recorded as dead, not quietly dropped.

---

## 1. THE INSTRUMENTS (new tonight, all env-gated, all inert when unset)

| env | what it does | file |
|---|---|---|
| `A7VK_GEOTRACE=1` | transforms every submitted vertex on the CPU by **the same mvp the shader gets** and reports: clip/NDC bbox, w range, vertices behind the camera, non-finite, object-space bbox, NDC vertex histogram, 2D screen bbox for the TL path, and the depth/blend/cull/shade states | `patch_geotrace.py` |
| `A7VK_GEOTRACE_ASCII=1` | prints the NDC histogram as ASCII — a picture of the geometry **with no rasteriser in the way** | `patch_geotrace.py` |
| `A7VK_GEOTRACE2` (`patch_geotrace2.py`) | print W/V/P **as the game set them**, with an orthonormality check; per-draw state histogram; **screen coverage** in units of screens | `patch_geotrace2.py` |
| `A7VK_AB="0:normal,3600:notex,…"` | **frame-scheduled A/B inside one run**: `normal` / `notex` (3D binds no texture) / `opaque` (3D ignores blend+alpha test) / `skip3d` (3D batches not recorded) / `vcol` / `vcolop` | `patch_ab.py` |
| `A7VK_TEXDUMP=<dir>` | writes every texture the RHI uploads to a PPM, plus mean RGBA and the fraction of zero-alpha texels | `patch_texdump.py` |
| `DECK_SURFTRACE=1` | what **the game asked for** at `CreateSurface` (bit depth + masks), every `Lock`/`Unlock` with a byte-level diff of what it wrote, and which surface each texture is uploaded from | `deck_dd7.cpp` + this round |

Built with `build32_rhi.sh` + `build-plain-race.sh`. Binaries:
`out-geo/cmr2` (md5 `75c2ec0b8005c8917a8923308082e734`) = `out-trace` (FE trace) +
both instruments. **`work/PLAY/out-plain/cmr2` and `out-race/cmr2` are untouched:
`b46d80fee5489834b87f843fd15484e0`, re-checked after every build this round.**

Runs: `geo1`, `geo2`, `geo3`, `ab1`, `vc1`, `td1`, `sf1`, `id1`, `aw1` — all in
`work/RACE/out/`. **0 GPU ring timeouts in every run** (kernel count 12 → 12);
the 23:09 RHI lifetime fix holds. A stage was reached and ran in every one.

---

## 2. WHAT THE RHI IS ASKED TO DRAW — draw families, in-race vs frontend

Per-frame rates, `DECK_DD7_CENSUS_EVERY=400` and the GEO instrument, 120-frame
windows in the same run (`geo3`):

| | frontend | **race, stage running** |
|---|---|---|
| 2D TL draws (`0x1c4`) per frame | **336** | **5** |
| 3D mesh draws (`0x2d2`) per frame | **0** | **286** |
| 3D vertices submitted per frame | 0 | **19,415** |
| TL vertices per frame | 1,412 | 20 |
| texture bound on a 3D draw | — | 285 of 286 |
| TL screen bbox | x 3…1259, y 37…751 | x −237…187, y −214…123 (off-screen corner) |

So the family that takes over at the handoff is exactly what `STAGE-REACHED.md`
said: **the indexed T&L mesh path**, ~286 draws/frame, and the 2D path collapses
to 5 draws/frame (which are not anywhere near the visible screen). **The visible
picture in a race is the 3D path, full stop** — proved by A/B in §4.

Indexed draw sizes per frame (log2 histogram; the *modal* draw is small):

```
4 indices : 155/frame      16 :  32/frame     64 : 18/frame    256 :  1/frame
8 indices :  22/frame      32 :  41/frame    128 : 15/frame   4096 :  1/frame
```

## 3. THE STATE THE 3D DRAWS CARRY — and this is the loud one

Per-frame histogram of the 3D draws by the state they were submitted with
(bits: z +2·zw +4·zf≤ +8·blend +16·alphaTest +32·lighting +64·fog +128·cullCCW),
`GT2` in `geo3.log`:

```
29 :120   63 :360   156 :840   157 :11167   191 :21840     (sum 34,327 = all 3D draws)
```

| mask | z | zw | zf≤ | **blend** | **atest** | light | fog | ccw | per frame |
|---|---|---|---|---|---|---|---|---|---|
| 191 | ✔ | ✔ | ✔ | **✔** | ✔ | ✔ | – | ✔ | **182** |
| 157 | ✔ | – | ✔ | **✔** | ✔ | – | – | ✔ | **93** |
| 156 | – | – | ✔ | **✔** | ✔ | – | – | ✔ | 7 |
| 63  | ✔ | ✔ | ✔ | **✔** | ✔ | ✔ | – | ✔ | 3 |
| 29  | ✔ | – | ✔ | **✔** | ✔ | – | – | ✔ | 1 |

**Every single 3D draw in the race is submitted with ALPHABLENDENABLE=1 and
ALPHATESTENABLE=1.** Blend factors at the time: SRCBLEND=5
(`D3DBLEND_SRCALPHA`), DESTBLEND=6 (`D3DBLEND_INVSRCALPHA`). And 100 of the 286
draws per frame have **ZWRITE off**.

That is not a port bug on its own — the game asked for it. It becomes the wall
when combined with §5: **the road is being drawn as a ~40 %-opaque wash, ~21
screens of it per frame, over the clear colour.** Which is exactly what the
captures look like: low-contrast mottle around `0x9cb4ac`.

## 4. THE A/B — one run, same scene, four pictures

`A7VK_AB` switched the mode by frame number and `A7VK_SNAP` took the captures at
fixed frames, so nothing here is a run-to-run difference. `vc1` / `vc.ppm.*`:

| frame | mode | screen covered by the clear colour | what the frame is |
|---|---|---|---|
| 2900 | normal | 97.6 % | thin band + a patch (the camera's moment) |
| 3100 | vcol (vertex colour) | 93.8 % | visible geometry, black+green mixed |
| 3300 | vcol + opaque | 95.7 % | visible geometry, 0.8 % pure black |
| 3500 | opaque (blend+atest off) | 93.5 % | **structured** — bright band, hard edges |
| 3700 | notex (3D binds no texture) | 67.5 % | **32.5 % of the frame is exactly `#ffffff`** |
| 3800 / 4200 | skip3d | 99.8 % / 100.0 % | **nothing but the clear colour** |

Three things fall out, and they are the round's real result:

1. **`skip3d` → the frame is the clear colour and nothing else.** The 3D path is
   the entire visible picture. (Same in `ab1`: 99.2 % and 99.8 %.)
2. **`notex` → the geometry renders as flat opaque WHITE over 32.5 % of the
   frame.** So the geometry, the transform, the culling, the rasterisation and
   the compositing are all **working** — the scene is drawn, on screen, opaque,
   at a plausible size. What is missing is not geometry.
3. With the game's textures bound the same geometry is nearly invisible
   (97.6 % clear colour at f2900). **So the visible failure lives in what the
   texture sample delivers** — its alpha (§5.3) and its contents (§6).

**A hypothesis killed here, by measurement, and it was mine:** "the W/V/P the
port composes is wrong" — i.e. the whole family that would make a scene land in
the wrong place. It is not. `GT2` printed the three matrices as the game set
them, at six different moments in the race:

* **WORLD** = identity throughout.
* **VIEW** = a proper rigid transform: |row0|=|row1|=|row2|=1.0000, row0·row1=0.0000,
  last row `(0,0,0,1)`, translation moving in the hundreds of units as the camera
  travels the stage.
* **PROJ** = a textbook D3D row-vector perspective:
  `[x 0 0 0 / 0 y 0 0 / 0 0 Q 1 / 0 0 −Q·zn 0]`, changing with FOV
  (y = 2.10 → 18.20 as the attract camera zooms).
* and the port's composition `(W·V)·P` in row-major float order is exactly D3D's
  `v·W·V·P` under the memcpy, and the Y-flip negates precisely the four
  coefficients of `out.y`. Checked by hand against the printed numbers, not by
  reading the comment.

Also killed by measurement: **projected texture coordinates are not in play.**
`D3DRS_TEXTURETRANSFORMFLAGS` (TSS t24) is dropped by the port with **last value
0 = TTFF_DISABLE**, and `TSS t11 TEXCOORDINDEX` last value 0. The game is not
asking for projective texturing in this scene.

## 5. WHAT IT IS DROPPING — the list, now with the values the game asked for

`DECK_SURFTRACE`/census at the end of `aw1`, race phase, with the **last value**
of each dropped call. This is the answer to "what is ignored", and until tonight
the census printed counts without values:

```
DROPPED render states (raw DX7 state: calls(last value))
  136 CLIPPING             28793(last=1)
  141 COLORVERTEX          41403(last=1)
  145 DIFFUSEMATERIALSOURCE 41403(last=0 = D3DMCS_MATERIAL)
  146 SPECULARMATERIALSOURCE    1(last=0)
  147 AMBIENTMATERIALSOURCE     1(last=0)
  148 EMISSIVEMATERIALSOURCE    1(last=1 = D3DMCS_COLOR1)
   60 TEXTUREFACTOR          878(last=0x75000000)   <- NEW in the race; 0 calls in the frontend
    2 ANTIALIAS / 4 TEXTUREPERSPECTIVE(1) / 8 FILLMODE(3) / 33 / 41 COLORKEYENABLE(0)   1 call each

DROPPED texture stage states
  s0/t11 TEXCOORDINDEX          193469(last=0)
  s0/t18 MIPMAPLODBIAS          191589(last=1)
  s0/t20 MAXANISOTROPY          191589(last=0)
  s0/t24 TEXTURETRANSFORMFLAGS  193469(last=0 = DISABLE)
```

* The same list appears in the frontend, at ~1/20th the rate. So nothing *starts*
  being dropped at the handoff in the way `STAGE-REACHED.md` framed it (the 20×
  jump is volume, not novelty); what is new is that **the stage drives the list at
  a rate where it matters**, plus `TEXTUREFACTOR`, which only the race sets and
  which the port has no combiner argument for (`D3DTA_TFACTOR` → unmapped, and
  `unmappedTexArgs` is 0, so no draw currently reads it).
* **`DIFFUSEMATERIALSOURCE = 0 = D3DMCS_MATERIAL`, not COLOR1.** The port's
  hardcoded "material colour source" (`flags[3]=0`) is therefore *right*, and the
  `vcol` A/B was testing a state the game does not actually use. Recorded because
  the plausible-looking fix "use the vertex colour" would have been wrong.
* `SetMaterial` is called **once in the whole run**, so the material is a
  constant — and `notex` proves it is opaque white, which the primitive renders
  as white. Nothing here is being drawn with a black material.
* `refusedFvf=0`, `clamps=0`, `refused_by_backend=0`, `unmappedTexOps=0` in every
  race census: **nothing in the draw path is being refused.** The picture is
  wrong, not absent.

## 6. THE ROAD IS NOT WHERE IT LOOKS — it is what the texture carries

`A7VK_TEXDUMP` wrote every uploaded texture to a PPM (`/tmp/texd`, 200 files,
`td1`), with mean RGBA and zero-alpha fraction. Splitting at the log line where
`Race_LoadSelectedStage` returns:

* **frontend textures** (ids 0–20, before the stage loads): 1–2 distinct colours,
  **90–100 % of texels equal their left neighbour** — a mask/logo, i.e. an image.
* **stage textures** (ids 21–76 and 85–99, all uploaded *after* the stage load):
  128×128 and 64×64, **0.3–2.1 % of texels equal their left neighbour**,
  **~25 % of all texels are one single constant colour**, and **mean alpha 75–154
  of 255** — the terrain textures arrive 30–60 % transparent.
* a few (ids 77–84, in the same phase) are clean, 86–96 %.
* the **frontend textures have the same shape of alpha**: mean alpha 15–46 with
  70–90 % zero-alpha texels. Those are masks and they render correctly, because
  the frontend draws them through the 2D path.

**Then the surfaces.** `DECK_SURFTRACE` says the game is asking for what the port
gives: `CreateSurface` with `dwRGBBitCount=32, R=ff0000 G=ff00 B=ff A=ff000000`
(the 181 surfaces at `bitcount=0` are `DDSCAPS_*` queries, not textures), and
`Lock` returns pitch = 4·w. The game then writes densely — for a 64×64 system
texture, **13,004 of 16,384 bytes changed across all 64 rows**. So the old
suspicion, "the port hands out 32-bit and the game writes 16-bit", is **false**.

The one thing that does not line up, and it is where the next round starts:

* the surface the RHI **uploads** from is **not the surface the game wrote**:
  `ST_UPLOAD … wroteByGame=0` on the uploaded object, while the objects the game
  `Lock`ed are different pointers.
* the data **as the game wrote it** is itself measured, at `Unlock`, with the same
  two numbers as the dump: frontend-phase writes 72–91 % adjacency (images), and
  the stage-phase writes **0.8–28 %** — better than the 1.1 % that gets uploaded,
  but still not an image.

So the trail now runs *inside the stage's texture path*: something between the
`.bfl` payload and the surface that gets bound is not producing an image, and it
is not the format the port advertises and it is not the RHI's upload.

## 7. WHAT MOVED, WHAT DID NOT

**Moved (new, measured tonight):**
1. The race-phase draw family and its state are now **named, counted and
   stateful** (§2, §3) — 286 mesh draws/frame, 19,415 vertices, 5,358
   fully-on-screen triangles, **21 screens of triangle area per frame**.
2. **The 3D path is the whole picture** and **its geometry renders opaque white
   without a texture** (§4) — so the failure is not geometry, transform, culling
   or rasterisation. Three plausible suspects are dead in writing.
3. **The dropped-state list now carries the values the game asked for** (§5), and
   `DIFFUSEMATERIALSOURCE=MATERIAL` kills the attractive-but-wrong fix.
4. **The stage textures are measurably not images** (§6) and the road's 30–60 %
   alpha is measured, not inferred.
5. Six new instruments, all gated, reusable (§1).

**Did not move:** the picture. There is still no road on the swapchain. What is
different is that the reason has been narrowed from "the stage is not drawn" to
**"the stage is drawn white, opaque, in the right place, and the texture that is
supposed to make it a road arrives non-image-like and half transparent"** — two
named faults with two named instruments pointed at them.

**Unchanged and re-verified after every build:** `work/PLAY/out-plain/cmr2` =
`work/RACE/out-race/cmr2` = `b46d80fee5489834b87f843fd15484e0`; `out-trace/cmr2` =
`efab9cdc317ec06b6d22d945f305d4fa`; 66/66 game objects; `deck_dd7.o` and
`a7_vk32.o` in the tree are now the instrumented builds (**both inert unless the
env vars are set** — with them unset the binary behaves exactly as before, which
is what the frontend numbers in `geo3.log` show, unchanged from `STAGE-REACHED.md`).

## 8. THE ORDER I WOULD WORK IT NOW

1. **Where does the stage's texture data stop being an image?** Instrument the
   copy between the surface the game writes and the surface the RHI uploads
   (they are different objects — §6). If a `Blt`/`CopyRects`/`UpdateTexture`
   stands between them, that is the port's code and it is a fix.
2. **Dump a stage texture from `KENlot.bfl` off disk** and compare it with both
   surfaces. That separates "the file read is wrong" from "the copy is wrong"
   from "the game's own decode is wrong", and it is the only step that needs the
   container format rather than the running game.
3. **Then** the alpha question: with a correct texture the 30–60 % alpha may be
   perfectly correct D3D (the game asked for blending on every draw). Do not
   "fix" the blend before the texture is right — `opaque` in §4 already shows it
   becomes a *different* wrong picture, not a road.
4. `TEXTUREFACTOR` (state 60) is new in the race and unmapped. Cheap to carry
   through `map_texarg` when something actually reads `D3DTA_TFACTOR`.
