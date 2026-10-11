# THE COPY PATH — the stage's textures were never decoded

`work/RACE`, 2026-10-11 ~04:2x EEST. Round: **step 1 and 2 of `WHAT-IS-DRAWN.md` §8,
in that order, plus the alpha question once the texture was right.**

Every number here is measured on this Deck, in the game's own binary, tonight.
Where a sentence disagrees with anything written before it, this file wins and
says so. One thing was found, it was port code, and it is fixed.

Read with `WHAT-IS-DRAWN.md` (the previous round) and `CHECKPOINT.md` (how to
re-run). The question this round was handed:

> 1. Instrument between the surface the GAME writes and the surface the RHI
>    uploads. Establish whether a Blt / BltFast / CopyRects / UpdateTexture sits
>    between them. If one does, that is port code and it is a fix — name the call
>    site.
> 2. Dump one stage texture straight out of `KENlot.bfl` on disk and compare it
>    against (a) the surface the game wrote and (b) the surface the RHI uploaded.
> 3. Only then the alpha question.
> 4. `TEXTUREFACTOR` if it does not derail 1 and 2.

---

## 0. THE ONE LINE

**The stage's textures are DXT5 and the port was copying the *compressed blocks*
into a 32-bit texture surface as if they were pixels.** The copy is
`CGraphics::LoadDDSTexture` → `DeckSurface7::Blt` (`deck_dd7.cpp`), the blit did no
format conversion, and a real DirectDraw driver does exactly that conversion — the
game never decodes DXT itself, it hands the block bytes to a blit and relies on the
driver. With the decode in place the Kenya terrain renders in its own colour
(measured, §5) where it rendered chromatic noise.

---

## 1. THE COPY PATH — found, and named

Two instruments, both new, both gated, both inert when their env var is unset:

| env | what it does | file |
|---|---|---|
| `DECK_SURFTRACE=1` (extended) | `CreateSurface` now reports the **FourCC and dwLinearSize it was asked for**, and `Blt` prints dst, src, both formats and the first bytes of each — the copy, logged at the only place a copy can be | `patch_textrace.py` |
| `DECK_FE_TRACE=1` (extended) | `LoadDDSTexture` prints the texture **name**, its DDS header, and the surface pointer it ends up on; `LoadTGATexture` likewise | `patch_textrace.py` |
| `A7VK_TEXUSE=1` | screen coverage **per bound texture id** (see §5) | `patch_texuse.py`, `patch_texuse2.py`, `patch_texuse3.py` |

The joined chain, verbatim from `out/tx1.log` and `out/tx2.log`. This is the whole
of step 1 and it names the call site:

```
[STAGE] stage file '.\Game\Tracks\KENYA\KENlot.bfl': didFileLoad=1 size=2400320
[TEX] DDS .\Game\Tracks\KENYA\KENlo.DDS 64x64 fcc=DXT5 bits=0 hdrflags=0xa1007 linearSize=4096 data=0xee362c90
[SURF] CREATE 64x64 caps=0x401808 asked: bitcount=0 flags=0x4 ... FOURCC=0x35545844 (DXT5) linearSize=4096
[SURF] LOCK 0x2cd18f0 ... pitch=256
[SURF]  UNLOCK 0x2cd18f0 64x64 pitch=256: changed=5546 of 16384 bytes, rows=64, maxrowbytes=156
[SURF]   ASWRITTEN 0x2cd18f0 64x64 meanRGBA=(25 56 49 16) adjSame=1.1%
[TEX]   -> surf=0x2ae9010 temp=0x2cd18f0 flags=0000b000 DXTflags=DXT5
[SURF] BLT dst=0x2ae9010(64x64 fcc=00000000 bpp=0 tex=1) <- src=0x2cd18f0(64x64 fcc=35545844 bpp=0) srcbytes=6465000000000000 dstbytes=0000000000000000
[SURF]  ST_UPLOAD 0x2ae9010 64x64 pitch=256 id=-1 wroteByGame=0 dirty=0
```

Four things fall out, and each was one of the questions on the list:

1. **The Blt is the copy.** `deck_dd7.cpp`, `DeckSurface7::Blt`, reached from
   `Graphics.cpp:3119` (`pTexture->pSurface->Blt(NULL, pTemp, NULL, DDBLT_WAIT,
   NULL)` inside `CGraphics::LoadDDSTexture`). `BltFast` delegates to it; there is
   no `CopyRects` and no `UpdateTexture` anywhere in the game's path.
2. **The source surface is a *DXT5* surface.** `FOURCC=0x35545844` — it was created
   from the DDS header, which is the point: the driver is being told "this is a
   compressed surface" and is expected to act like one.
3. **`wroteByGame=0` is now explained, not a mystery.** The game `Lock`s `pTemp`
   (the system surface that holds the block bytes) and never Locks
   `pTexture->pSurface`; the uploaded surface is the *destination* of the blit.
   The two pointer sets §6 of `WHAT-IS-DRAWN.md` could not reconcile are the two
   ends of one blit.
4. **The source bytes are the compressed stream.** `srcbytes=6465000000000000` is
   `a0=0x64, a1=0x65` then six zero index bytes — the first DXT5 block, read out
   of the source surface at blit time. Nothing has decoded anything.

**The 181 stage DDS textures are all DXT5** — `grep 'TEX] DDS' | sed fcc` gives
`181 DXT5`, zero of anything else. The frontend and the cars are **TGA**, and
`CGraphics::LoadTGATexture` decodes those in software, correctly. That is the whole
reason the frontend looks right and the stage did not: the port implemented the
decoder the frontend needs and not the one the stage needs.

## 2. THE OFF-DISK CONTROL — the file is fine, the blit was not

`bfl.py` reads a `.bfl` the way `GetGenericFileBuffer`/`FindFileInArchive` do
(gzip, `CMPR` header, directory at `payload + *(int*)(payload + size − 4)`, entries
`{id, offset, nameLen, name}` padded to 4) — transcribed, not guessed.

`KenLot.bfl`, on disk, 2026-10-11:

```
KenLot.bfl: payload 2400320 bytes, 370 entries, dir@2391424
  rd_220ja.dds  64 64 bits=0 fourcc=DXT5 blobsize=4224
  ... 370 entries, every one 'DDS ', every one 64x64 DXT5, every one 4224 bytes
```

**370/370 entries are `DDS ` magic, DXT5, 64×64, 128-byte header + 4096 bytes of
blocks.** A 64×64 DXT5 texture is exactly 16×16×16 = 4096 bytes, so the file's
arithmetic is self-consistent. **The read is right.**

Decoded, they are terrain: `ter_kn4a.dds` (the texture that paints the frame,
§5) comes out at mean RGB **(116, 89, 17)**, alpha **255 everywhere**, and
`ter_kn3a.dds` likewise (alpha 255, 4096/4096 texels).

**The decoder was checked against an independent one** — `ffmpeg -i x.dds
-f rawvideo -pix_fmt rgba`, which is a separate implementation:

* **alpha: 4096/4096 texels identical on every file tested.** The 8-value/6-value
  table, the 3-bit index order and the block layout are right.
* **RGB: within 1 LSB**, and both remaining 1-LSB decisions were settled by
  measurement rather than by taste (`patch_dxt_exact.py`): 565→888 expands by
  **bit replication** (`(r5<<3)|(r5>>2)`), and in DXT5 the 4-colour interpolation
  is used **even when c0 <= c1** — the 3-colour mode is DXT1 only. Measured on
  block 62 of `kenlo.dds` (c0=0x4209, c1=0xffff): ffmpeg's second colour is
  `(2A+B)/3 = 129`, not the midpoint `(A+B)/2 = 160`.

**And the uploaded surface, reproduced from the file.** `cmp_model.py` rebuilds,
for every entry of the archive, the surface the game's copy would produce, and
scores it against the dumped upload:

```
tex021.ppm 64x64 vs 370 entries of KenLot.bfl
     53.4%  strided  car_pk1b.dds   off=1136320        <- chance
```

and for the first texture of the stage load, against the file it names:

```
kenlo.dds -> strided model   RGB match 100.00% (4096/4096)
          -> linear  model   RGB match  47.92%
```

Strictly, after the `DDSD_LINEARSIZE` fix (§3) the game takes the *linear* branch
and the match is exact from the contiguous block stream; before it, the port's
clobbered `DDSURFACEDESC` made the game take its **else**-branch, whose source
pointer advances by `desc.dwHeight` (64 bytes) per row while copying 256 — the
`strided` model above. The `UNLOCK ... rows=64` versus `rows=16` in the write
trace is that difference, and it is why the fix has two halves rather than one.

**So: read-right, decode-right, copy-wrong.** Every other question is downstream
of that answer, exactly as the order said.

## 3. THE FIX — three edits, all in port code

`patch_dxt_blt.py` + `patch_dxt_exact.py`, both idempotent, both backing up first.

1. **`DeckSurface7::Blt` decodes DXT1/DXT5** when the *source* surface was created
   with a DXT FourCC. That is the format conversion the blit is being asked for.
2. **`surface_fill_desc` reports a compressed surface as compressed** — `DDPF_FOURCC`
   plus `DDSD_LINEARSIZE`/`dwLinearSize` — instead of claiming every surface is
   32-bit A8R8G8B8. This is what a driver returns from `Lock`, and the game already
   has the branch for it (`Graphics.cpp:3060`): `memcpy(lpSurface, pSrc,
   dwLinearSize)`. Without it the game's row loop smears the blocks as well as
   leaving them compressed — measured, `rows=64, maxrowbytes=156` before,
   `rows=16, maxrowbytes=156` after.
3. **`Blt` marks its destination dirty**, so a surface blitted into after it has
   already been uploaded is re-uploaded. Latent today; one line. Cost measured:
   `textureUploads` 260 in 3943 frames before, 346 in 6303 frames after — the same
   rate, so no re-upload storm.

`CreateSurface` now records the FourCC unconditionally (it used to record it only
when the trace was on, and recorded no FourCC at all, so a DXT surface and an RGB
surface were indistinguishable downstream), and `BltFast` inherits all of it.

**The game's own flags changed as a consequence, for the better.** Before the fix
the post-load flags were `0000b000` — the game believed it had a DXT5 *texture
surface* while the port handed it 32-bit storage, a lie it then acted on. After,
`00009001`: the game asks `CreateSurface` for `bitcount=32 flags=0x41
R=ff0000 G=ff00 B=ff A=ff000000`, i.e. the 32-bit surface the port actually
provides, and the DXT decode happens in the blit. That is a coherent pair of
statements for the first time.

## 4. THE ALPHA QUESTION — answered, and the answer is "there was no second fault"

Item 3 said do not touch the blend before the texture is right. It was right to
wait, because **the alpha was the same fault measured in a different channel.**

| | before | after |
|---|---|---|
| stage texture, mean alpha | 75–154 of 255 | **255** (terrain) |
| stage texture, zero-alpha texels | 18–85 % | **0.0 %** |
| `ter_kn4a.dds`, alpha | — | **255 on 4096/4096 texels**, verified against ffmpeg |
| uploaded bytes of `kenlo.dds` vs its own decode | non-image | **4,090/4,096 texels identical** (the residue is the 1-LSB rounding of §2) |

The "30–60 % transparent terrain" was compressed block bytes being read as ARGB.
No blend state was touched, no render state was touched, and none of it was needed.

**What is *not* a fault and must not be "fixed": the draw state.** Every 3D draw in
a race is still submitted with `ALPHABLENDENABLE=1`/`ALPHATESTENABLE=1`,
SRCALPHA/INVSRCALPHA — the game asks for that, the terrain now arrives opaque, and
`notex` still proves the blend works (no texture → flat opaque white over 32.5 % of
the frame). The remaining wash on the frame is §5's dust, which is the game's own
content.

## 5. WHAT PAINTS THE FRAME NOW — named, because "which texture" was never asked

`A7VK_TEXUSE=1` accumulates the NDC area of every 3D draw against the texture bound
to it, and prints a histogram every 400 frames. It exists because after the fix the
frame is a coherent *picture* and colour statistics cannot say whether the soft
region in the middle is the road, the sky, or geometry sampling the wrong texture.

Coverage summed over a whole run (`tx7`/`tx8`), with the texture each id is:

| texture | coverage | what it is |
|---|---|---|
| `TRACKS\KENYA\TRACKTEX\PCLOW\TER_KN4A.DDS` | 1,689,012 | **the terrain** |
| `NEWIMAGE\skid\skids_blank.DDS` | 124,301 | skid marks |
| `NEWIMAGE\DustCld\23_5\D_000..007.DDS` | 9,951–17,387 each | **8 dust clouds** |
| `TRACKS\KENYA\OBJECTS\PCLOW\BLD_30A.DDS` | 5,451 | a building |
| `NEWIMAGE\GRASS\GR_001.DDS` | 2,025 | grass |

And the picture, read off `tx7.03_t70.ppm` (each cell 20×20 px): clear sky above
y≈210; a bright dust mass from y≈220 to y≈620 with a small dark object inside it
at x≈880–1040 — the car; then a **full-width ground band from y≈660 to y≈760,
dirt-brown across the middle with grass-green at the edges**; clear colour below
and at the sides.

**The ground band's mean colour is (115, 90, 15). `ter_kn4a.dds`'s mean colour is
(116, 89, 17).** That is the terrain being painted with its own texture at
effectively full brightness — and the same band before the fix was (142, 171, 159),
i.e. noise.

The frame as a whole, same instrument, before vs after:

| | before (`tx1`) | after (`tx6`) |
|---|---|---|
| distinct colours / sampled pixels, race frame | 268,522 / 385,323 | **22,187 / 589,792** |
| most common non-clear colours | `#6cdfdb`, `#99a497`, `#9aa498` — chromatic noise | `#d6d7d6`, `#c5c5c5`, `#806d12` — greys and a terrain brown |
| ground band | noise | the terrain texture's own colour |

The greys are the dust clouds, and they are legitimate: `d_000.dds` decodes to
alpha mean 130, min 0, max 255, with 553 of 4096 texels fully transparent and the
rest spread across the range — a soft puff, exactly what a dust billboard is.

## 6. TEXTUREFACTOR (item 4) — measured, and it cannot matter today

`TEXTUREFACTOR` is set 878 times in the race, last value `0x75000000`
(`Graphics_SetTextureFactorAlpha`, called from **`CarPhysics.cpp:898`** — it is the
car's own alpha). It is unreadable **by construction**, and this is the check:

* `D3DTA_TFACTOR` **does not appear anywhere in the game's source** (`grep -rn
  D3DTA_TFACTOR *.cpp *.h` → nothing), so no texture stage argument ever names it;
* `unmappedTexArgs=0` in every race census — the port's own counter for "an argument
  I could not map" is zero, which independently confirms it;
* **and stage 1 is never bound at all** — `A7VK_TEXUSE=1` with stage-1 accounting
  (`patch_texuse3.py`) prints `STAGE1: distinct ids with coverage=0 … drawsBoth=0`
  for every window of a whole run. `FVF 0x2d2` carries two texture coordinate sets
  and the port has two stages, and the game uses one.

Carrying the value through `map_texarg` would change nothing. It is **not** done,
and this is why, measured rather than assumed.

## 7. WHAT IS STILL WRONG — and it is measurable

**The road's own textures never reach a draw.** Of the 40+ Kenya track textures
loaded and uploaded, exactly one — `TER_KN4A` — is ever bound in a race:

```
id=86  RD_K520B.DDS   cov=0.0  draws=0
id=90  RDKN_T1.DDS    cov=0.0  draws=0
id=102 RD_GA.DDS      cov=0.0  draws=0
id=103 RD_GB.DDS      cov=0.0  draws=0
id=104 RD_GC.DDS      cov=0.0  draws=0
...  every RD_*, MR_*, MOU_*, TRE_*, BSH_* : cov=0.0, draws=0
id=95  TER_KN4A.DDS   cov=1689012.2  draws=4389
id=..  GR_001.DDS     cov=2025.1     draws=6620
```

So the ground is painted with the *terrain* texture and the road-surface textures
are unused. Whether that is the game's own material selection or the port answering
a selection query wrongly is **the next question and it is not answered here.**

**Also still open, carried forward unchanged:** the near-plane blow-ups pollute the
coverage numbers (single triangles project to NDC x ≈ 1.2e4, y ≈ 2.2e4 — capped at
25 screens each in the instrument, so `TER_KN4A`'s 1.69 M is a floor, not a
measurement of terrain area); the camera framing at the captured moments puts the
horizon at y≈665 of a viewport whose top is y≈200, so only ~17 % of the viewport is
ground — plausible for a chase camera in a dust cloud, not verified; and mipmaps
are never created (`GetAttachedSurface(DDSCAPS_MIPMAP)` answers `DDERR_NOTFOUND`),
so 64×64 terrain textures minify with aliasing.

## 8. STATE OF THE DISK

| file | change |
|---|---|
| `port/rhi/deck_dd7.cpp` | DXT decode in `Blt`; `Lock`/`GetSurfaceDesc` report compressed surfaces as compressed; `CreateSurface` records the FourCC; blit marks the destination dirty |
| `port/rhi/a7_vk32.c` | `A7VK_TEXUSE` (coverage per bound texture id, both stages) — **inert unless set** |
| `port/tree/CMR2Decomp/Graphics.cpp` | `LoadDDSTexture`/`LoadTGATexture` name trace — **inert unless `DECK_FE_TRACE`** |
| `work/PLAY/out-plain/cmr2` | **now `e43d7a1e4279fe2c05647c7c053d4fd3`** (was `b46d80fee5489834b87f843fd15484e0`, kept as `cmr2.bak-pre-dxtblt-20261011`) — this is what `~/lena/cmr2-native.sh` runs, and it carries the fix |
| `work/RACE/out-race/cmr2` | same binary, same md5; the previous one kept as `cmr2.bak-pre-dxtblt-20261011` |
| `work/RACE/out-tex/cmr2` | the build these results were measured on |
| `work/RACE/` | `bfl.py`, `bfl_dds.py`, `dxt.py`, `cmp_model.py`, `find_src.py`, `search_src.py`, `look.py`; `patch_textrace.py`, `patch_dxt_blt.py`, `patch_dxt_exact.py`, `patch_texuse.py`, `patch_texuse2.py`, `patch_texuse3.py`; logs `out/tx1..tx8.*` |

**Runs `tx1`–`tx8`: 0 GPU ring timeouts (kernel count 12 → 12, unchanged every
run).** A stage was reached in every one. 66/66 game objects untouched; the
instrumented port objects are inert with their env vars unset.
