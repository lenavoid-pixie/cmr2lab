# progress.md — Lena on the Deck, reporting to Lena on the phone

Rewritten **whole** at every checkpoint, never appended. If a line is not true at
the timestamp below, it is not in this file. Every line answers "how do I know
this" with a number, a path, or a test result — and where I could not answer it,
the line is in §6 instead.

**Last whole rewrite: 2026-10-10 14:54 EEST. §0 was appended at 15:15 by the
pretty-frame worker; the rest of the file is that 14:54 rewrite, unverified by me.** This file is one commit — the rewrite
that carried it is `git log -1 -- progress.md`, and it is rewritten whole, not
appended, so there is never a stale claim above a fresh one.

**Blocks §0, §0.5 and §0a were appended after that rewrite, each saying so at its
own head. §0a has since been re-verified in place, with every change marked
`CORRECTION` and a stamp at the top of the block.**

**On provenance, because this rewrite is 26 minutes after the last one.** §1, §2.8,
§3A and §5 are measured at 14:47, and one §6 bullet was added at 14:54. Everything else in §2 is carried forward from the
14:17–14:28 rewrite and I did **not** re-run it in this pass; those lines say so
where it matters. Nothing here has been re-derived from memory.

Paths, so they line up on both ends:

| short name | real path on the Deck |
|---|---|
| the install | `~/lena/.lena_cmr2/game` (2.26 GB retail PC install; 259 `.c3d`, 220 car containers) |

## 0a. THE MIRRORED FLANK — CLOSED. added 2026-10-10 18:27 EEST — cited by *subject*, not by hash, because this repo is rebased onto `origin/main` under it: `git log --oneline --grep='flank mirroring'` finds it (it said 18:35 EEST here at first, later than the commit that carried it; corrected in the re-verification pass)

**Question, from the phone, and nobody else's queue:** in the 4-car frame and the
single 205 frame the decals on one flank read correctly and the lettering on the
other looks MIRRORED. Is that faithful to the original game, or is it a bug in our
texture binding?

**ANSWER: FAITHFUL. It is in the file, and the engine draws the file. Not a
binding bug. Closed — no fix, and a "fix" would be a regression.**

**This block, too, is appended by a worker who is not the author of the rest of
this file.** Everything below §0a is carried forward untouched. The one edit I
made outside this block: the path table above had lost its last row into the §0
heading; I completed that row with the install path I actually measured against
(`~/lena/.lena_cmr2/game`), which is all it was missing.

**Re-verified and corrected 2026-10-10 18:31 EEST by the second worker on this same
job** (the first pass committed and then ran out of steps; it left no checkpoint, so
this pass re-derived everything instead of trusting its notes). All of the commands
below were re-run on the Deck and the engine paragraphs were re-read in
`~/.lena_cmr2/upstream/CMR2Decomp`: `Game_DrawMeshTextureRuns` (`Game.cpp:2114`) binds
the file's own per-triangle texture and nothing else; `SetTexCoordIndex`
(`Graphics.cpp`) sets `D3DTSS_TEXTURETRANSFORMFLAGS, 0` on every path it takes.
**The verdict is unchanged — FAITHFUL, not a binding bug.** Three things in this block
were wrong and are marked `CORRECTION` inline: one over-claimed line about one-sided
textures, one row of the sample vertex table that was a mis-paired vertex, and the
header time. `tools/flank_mirror.py` grew a `BODIES` mode and a `PAIRS` dump so the
headline numbers now reproduce **verbatim** from the command list, which the earlier
list did not actually do.

Commits, if a hash is wanted: `git log --oneline --grep='flank mirroring'` gives the
two round-1 commits and `--grep='mirror-flank: re-verified'` the round-2 one. Hashes
moved twice while this block was being written (an earlier draft cited `49d45ad`,
which no branch points at now) because another session is pushing to `main` — hence
the subjects above. `git ls-remote origin refs/heads/main` is the check that a push
landed.

### The three questions the phone asked, answered in order

**1. Does the engine mirror a body panel?** No — and there is no code path that
could. `Game_DrawMeshTextureRuns` (CMR2 `0x0049c510`) walks the mesh's own
triangle list and, per triangle, binds
`*(int *)((BYTE *)pTri + 4 + pTri->field_0x2c * 4)` — the file's own texture
index — then `DrawIndexedPrimitiveVB(D3DPT_TRIANGLELIST, ...)`. The only
transform applied to the vertex data is the node's world matrix
(`SetTransform(D3DTRANSFORMSTATE_WORLD, pNode->worldF)`, `Game_DrawViewMaskNode`
`0x0049cad0`; `Graphics_DrawMeshLOD` `0x0049c940` is the wrapper). No UV
transform, no per-side texture selection, no mirror flag on a mesh.
The engine's *only* mirror is `StageObject_RebuildMirroredTiltMatrix`
(`0x00486910`), and reading its basis swap — `right' = forward`,
`forward' = -right`, `up' = up` — gives determinant **+1**: it is a 180° rotation
about the up axis, **not** a reflection, and it is used for view/camera records.
The car's own wheel nodes in the file are that same rotation
(`205a1N` nodes 1–4: `diag(-1,1,-1)`, translation ±0.717/±0.718 in z), which is
how a wheel is placed on the other side without flipping anything.

**2. Is the left flank a mirrored instance of the right?** No. Both flanks are
real, separately tessellated geometry in the same file, under identity scene
nodes. `205a1N`: the body parts span z ∈ [-0.88, +0.88] with triangles on both
sides (`08BodySim` 101 faces at z>0.3, 83 at z<-0.3; `08BodySim` has V=247, an
odd count — the two flanks are not copies). Every body part's node is the
identity matrix, so the mesh data *is* the world data for panels.

**3. Is there a distinct LEFT texture in the trailer?** No. Over the 259 `.c3d` in
the install, **192/205 files with flank triangles use the *same* body/livery
texture on both flanks** (measured: the texture with the most triangles is in both
the left-flank and right-flank index sets). Over the **23 `*a1N` exterior car
files — the set that carries a livery, and the set in the frames — it is 23/23**,
printed by `BODIES` below. The 43 files whose left/right *sets* differ do so by one
underside / glass / single-panel index that my `|Nz| > 0.75` threshold caught on
one side only — the `a1N` five are `ap5unddf` (underbody), `AAQGliDf`, `aesglod2`,
`ASIGlODf` (glass) and `afpintdf` (interior); the body texture is in both sets on
all 23 bodies.

**CORRECTION — this paragraph first said "0 files have a left-only body texture"
and "the body texture is in both sets on every one of them", and both halves of
that are wrong as written.** Measured corpus-wide now: **2 files put their
most-used texture on the LEFT flank only** (`esca5`, `escc5`) and **11 on the
RIGHT only** (`205a5`, `205c5`, `cora5`, `ma1a5`, `ma1c5`, `ma2a5`, `ma2c5`,
`ma3a5`, `ma3c5`, `mita5`, `mitc5`). They are not a counter-example, and the
reason is measurable: all 13 are **`*a5` / `*c5` cockpit-interior files** (their
parts are `20interior`, `27swheel`, `22wiperL`/`22wiperR`, `28gearhand`,
`41Speedo`, `30dash`), so their most-used texture is a dash / door-card texture
and the triangles my flank band catches in them are interior trim and glass, not
artwork. Restricted to the files that carry a livery — the 23 `*a1N`, which is all
the `BODIES` mode counts — it is **23/23 on both flanks, 0 one-sided**. So there
is nothing for our binding to have got wrong: there is no second texture to bind.

### What the data actually does, measured

Mirrored triangle pairs (`(x,y,-z)`, matched vertex by vertex) over the 23 body
meshes, comparing texture coordinates at the mirrored vertices:

```
flank triangle pairs with SAME u at the mirrored vertices : 2025   <-- the answer
flank triangle pairs with u complemented (u' = 1-u)      :    1
neither (different tessellation on the two flanks)       :  425
```

Per car, `seaa1N` is 90 / 1 / 13 and `205a1N` is 66 / 0 / 30 — and the pairs are exact, not approximate.
Three vertices of one `seaa1N` (`08BodySim`) front-wing triangle and their
mirror partners, copied out of `python3 tools/flank_mirror.py PAIRS seaa1N --xyz`
(the matcher requires each vertex to have a partner within 2e-3 on x, y and −z):

```
P=(-0.829,-0.267,+0.896)  uv=(0.297,0.063)      P'=(-0.829,-0.267,-0.896)  uv=(0.297,0.937)
P=(-0.368,-0.492,+0.835)  uv=(0.420,0.003)      P'=(-0.368,-0.492,-0.835)  uv=(0.420,0.997)
P=(-0.424,-0.268,+0.830)  uv=(0.405,0.063)      P'=(-0.424,-0.268,-0.830)  uv=(0.405,0.937)
```

`x` and `y` are identical and `z` changes sign, exactly; `u` is identical to three
decimals at every mirrored vertex (0.297/0.297, 0.420/0.420, 0.405/0.405); `v` is
the complement (0.063/0.937, 0.003/0.997).

**CORRECTION — the table that stood here had a mis-paired row.** Its second row
was `P=( 0.176, 0.033, -0.835)` against `P=( 0.021, 0.035, +0.835)`: `x` does not
match, so it is not a mirrored pair at all, and its `u` differs by 0.042 —
i.e. it was a vertex from the `neither` bucket sitting in a table meant to show
the same-`u` relation. Rows 1 and 3 of it were real, and had been rounded by hand
to the vertex `z` rather than the triangle centroid; the rows above are verbatim
tool output instead. **It never touched the counts** (2025 / 1 / 425), which come
from the tool, not from the table.

**The 1 complemented pair, named, because it was the last loose end.** The tool
prints its example now, and it is a wrap seam, not a mirrored island: `u=0.004`
against `u=0.997` on `seaa1N` `05BonCamDay`. Every one of the **4** such pairs in
the whole corpus (`seaa1N`, `seac1`, `seac3`, `audc1` — one each) sits on those
seams (`u=0.004` vs `0.997`, `u=0.000` vs `0.999`, `u=0.014` vs `0.989`: the same
texel reached from the other end of 0..1). **4 pairs out of 12,484, all of them
at the seam, 0 genuine `u' = 1-u` pairs.**
Same `x` ⇒ **same `u`** (0.727/0.727, 0.405/0.405), and `v` is the complement
(0.820/0.180, 0.809/0.191 — `v' = 1 - v` to three decimals). So the livery's
"along the car" coordinate runs the same way round the car on both flanks, and
the texture's two halves are vertical mirrors of each other (normalised
  cross-correlation on that car's body texture, `seaa1N` `ASCDBoDf`, band
  `u[0.40,0.76] v[0.79,0.88]` against `u[0.40,0.76] v[0.12,0.21]`: **0.845 for
  `A` vs `flipud(B)`**, **-0.152 for `A` vs `fliplr(B)`**).

**That correlation, re-measured through a tool in the re-verification pass,** so
it is no longer a number with no script behind it:

```
TEXDUMP=/tmp/q2tex <build>/cmr2deck seaa1N --game ~/.lena_cmr2
python3 tools/flank_art.py /tmp/q2tex        # <- new, stdlib only
  NCC(A, B (no flip)       ) = +0.218
  NCC(A, flipud(B)         ) = +0.816     <- the claim
  NCC(A, fliplr(B)         ) = -0.176     <- and the other half of it
  NCC(A, fliplr(flipud(B)) ) = -0.030
```

Same relation, coefficients a little different from the 0.845 / -0.152 first
written down: a different viewer build's decode of the same `.bfl` block (the
texture is 1024x1024, so a 93x369-pixel band pair), not a different finding.
`flipud` strongly positive and `fliplr` negative in both passes is the whole
claim, and the sign of `fliplr` is the load-bearing part.
**What that buys:** the `v' = 1-v` mapping therefore lands both flanks on the
same artwork, upright — and the only difference left is the horizontal sense.
**Viewed from outside, one flank is the horizontally mirrored image of the other.** That is what the phone saw, and it is
in the file.

Two more measurements that close the escape routes:

- **UV set 1 is UV set 0.** Every vertex of every part of `205a1N` (1,204
  vertices), `foca1N` (1,144) and `6r4a1N` (1,360), 14 parts each: `uv1 == uv0`
  exactly, 0 differing vertices. So there is no second UV set holding a "correct"
  mapping that the game uses and the viewer does not. (The engine does select the
  second set for texture stage 1 in blend case 9 — `SetTexCoordIndex(param1, 1)`
  in `ApplyTextureStageChange`, `0x004a4850` — but the set it selects is a
  byte-for-byte copy of set 0, so it cannot change a livery. Case 11's
  `TCI_CAMERASPACEREFLECTIONVECTOR | 1` is an environment map.)
  Provenance: this is the viewer's own `uv1 == uv0` check re-run over three cars
  and every part, not the engine's call sites; the call sites are read from the
  decomp.
- **Our own render agrees with the file.** Flat-lit flank views
  (`AMB=1 GAIN=0 FILL=0 SHADOW=0 BG=0 OFFW=2000 OFFH=1000 ELEV=0 DIST=0.35`,
  `YAW=0` and `180`, car `seaa1N`), OCR'd with `tesseract` (`afr` is the only
  traineddata on this Deck): a competition number reads as **`OS`** (conf 82) in
  the frame as rendered and as **`20`** (conf 94) only after a horizontal flip of
  the frame; the other flank's text needs no flip. And the decals themselves are
  stored reading normally in the texture — `seaa1N` `maxon` @(512,834), `MoviStar`
  @(739,226); `cora1N` `MoviStar` @(748,252) and @(769,870); `6r4a1N`
  `COMPUTERVISION` @(244,248); `foca1N` `Movista` @(196,455) — so the art is not
  pre-mirrored anywhere in the texture; the mirroring is the *mapping*, not the
  picture.

### The honest limit

The step from "the file mirrors one flank" to "the game mirrors one flank" is the
engine's draw path (above), and it is a reading of the decomp, not a picture. The
one measurement that would replace the argument with a photograph is running the
retail game itself on a car-viewer screen and looking at both sides — the install
is on this Deck and there is a launcher, but the port cannot draw a car yet (the
D3D7 state tracker is still the M5 gap, §5/§6) and I did not fire the original game
up for this. If that run ever contradicts this
block, the block is wrong, not the run. But the game has no per-side texture to
bind and no UV transform to apply, so I do not expect it to.

### Do not "fix" it

There is no local fix. A global u flip in the viewer would make the mirrored flank
read and break the flank that already reads. The mirroring is a property of the
shipped art: the left flank's islands are UV'd the same way round as the right's.
The correct action is to record it and stop looking at it.

**Reproduce, from this repo:**

```
python3 tools/flank_mirror.py PAIR seaa1N 205a1N     # the pair relation, per car
python3 tools/flank_mirror.py PAIRS seaa1N --xyz     # every matched pair, one line each
python3 tools/flank_mirror.py BODIES                 # 23 *a1N bodies: 2025 / 1 / 425
python3 tools/flank_mirror.py PAIR --all             # the pair relation, all 259 cars
python3 tools/flank_mirror.py CORPUS                 # 259 .c3d: textures + u direction
python3 tools/flank_art.py <TEXDUMP dir>              # the texture-side correlation
```

`tools/flank_mirror.py` (stdlib only, parses the container the same way
`cmr2deck.c` does) prints exactly the numbers above. Two of these commands were
added in the re-verification pass, and they are the ones that make the headline
counts exact:

- **`BODIES`** prints `body/livery texture used by BOTH flanks: 23 / 23`,
  `mirrored-pair triangles: same u 2025  u complemented 1  neither 425`,
  `du/dx L+: 1076 L-: 257 R+: 1055 R-: 309`, `cars whose two flanks run u the
  same way: 23 (opposite: 0)`. The **2025 / 1 / 425 is the sum over the 23 `a1N`
  files**, which is what the block means by "the 23 body meshes".
- The earlier command list labelled `PAIR --all` as "all 23 body meshes". That was
  wrong: `PAIR --all` is **all 259** cars, and sums to **10476 / 4 / 2004** —
  interiors included. Anyone who followed the old list and added it up would have
  got a number that contradicts this block. Corrected here.
- `CORPUS`: `du/dx sign over outer flank triangles L+: 5843 L-: 887 R+: 5948
  R-: 1114 / cars whose two flanks run u the same way: 181 (opposite: 11)` is the
  same finding from the other direction. The 11 "opposite" files are **all
  `*a5` / `*c5` cockpit interiors** — nine of them carry a single flank-band
  triangle on one side against 18 on the other, the other two are `6r4a5` /
  `6r4c5` (29/2 and 35/1) and `corc5` (1/12). (The block first described these as
  "1–3 flank triangles each", which is true of the one-sided texture files, not of
  these; the substantive claim — they are interiors, not bodies — was right.)


## 0. THE FRAME MIAMI ASKED FOR — added 2026-10-10 15:15 EEST by the worker on
## the "make the port a pretty thing to look at" job.

**This block is APPENDED BY A DIFFERENT WORKER, at the top, on purpose.** The rest
of this file is carried forward from the 14:54 rewrite and I did not re-verify it.
Read §0 as mine, dated, and everything below as the previous worker's.

- **It is on GitHub, which is how the phone gets it.** `screenshots/cmr2deck-205a1N-1280x800.png`
  (one frame, 1280x800, 4x MSAA) and `screenshots/cmr2deck-205a1N-4views-1280x800.png`
  (four yaws, 640x400 each, stacked). Commit `66d32d1`, pushed 15:12.
- **`docs/FRAME-NOTE.md` is the honest note and it is the deliverable that matters.**
  It says line by line what came from the game's own code and what is mine. The
  one-line version: **the port does not draw this.** The *viewer* (`src/port/cmr2deck.c`)
  draws the game's *data* with our draw code, outside the game.
- **What changed, in one sentence:** the viewer now binds the texture each triangle
  names in the file (`MeshTriangle + 4`, `field_0x2c = 0`, the rule in
  `Game_DrawMeshTextureRuns` 0x0049c510) instead of matching part names against the
  texture table — and it uses the game's own alpha test (`ALPHAREF` 0x80/1,
  `D3DCMP_GREATER`) and blend pair (SRCALPHA/INVSRCALPHA). Before: every body panel
  was bound to texture 0, `AP5NWBDf`, mean RGB 17,17,19, which is why the car was a
  grey blob. After: 8 distinct textures over 27 runs, 0 runs refused.
- **Measured, not eyeballed** (`src/port/tools/preview.py --diff`, against a frame
  rendered with `NOCAR=1`): the car covers **28.8%** of a 1280x800 frame in 1089x509 px;
  its mean luminance is **76.6**; the culling direction is settled by measurement
  (`CULL=front` drops that to **28.8**, `CULL=back` ≈ `CULL=none`), so the faces kept
  are the outside ones.
- **Mine, labelled, and in the note:** the ambient value, both lights, the planar
  ground shadow, the backdrop, MSAA, the camera and the framing.
- **Biggest gap:** the D3D7 state tracker (277 of 298 call sites are state calls) still
  does not exist, so the port still cannot draw a car. This frame is the target image
  for M3 and the run-split/alpha/texture list above is the spec it has to reproduce.
- **The texture fix has a number, not an opinion.** `NAMETEX=1` renders the old
  name-matched path, so the two were diffed: car mean chroma **2.2 → 13.6**, mean
  luminance **23.1 → 72.7**, and 505 of 888 triangles moved off the wrong texture.
- **The path runs on all 259 cars**, not on the one I looked at:
  `docs/tables/texruns-sweep.tsv` + `src/port/tools/texruns_sweep.py` — 259/259
  completed, **0 cars with anything wrong**, 123,127 triangles, 3,873 runs, median
  15 runs per car, 0 runs refused. Slot roles are the same across four
  manufacturers and the dimensions land at 3.7–4.2 m by 1.4–1.5 m.
- **The textures in the frame are byte-exact against an independent decoder**:
  `tools/tex_verify.py` (Python, from the specs) vs the C viewer's own decode over
  the four cars in the pictures — `blocks compared: 108   mismatches: 0`.
- **Colour is not swapped** (the check that makes a render trustworthy): the car's
  mean colour in the frame matches that car's own body texture in channel order on
  all three cars tested, including the blue Impreza and the blue Metro 6R4.
- **The windowed path core-dumped and now does not** (`patch_swapfmt.py`: a Deck
  swapchain is `B8G8R8A8_UNORM`, not `R8G8B8A8_UNORM`, and SDL refuses a resolve
  across two formats). Verified: 120 frames in a real window, clean exit.
- **The frame is reproducible from the repo byte for byte**: replaying the five patch
  scripts over the pristine source, rebuilding and re-rendering gives the same md5
  (`91a8ad5135bf1171568b950f95d22598`) and 0 differing pixels.
- **Not claimed:** no human has looked at this frame. Miami is the first. And 258 of
  the 259 cars in the sweep have never been looked at at all — that sweep is counts.

---

## 0.5. t2 CLOSED, and the two source steps of §5 done — appended 2026-10-10 18:25 EEST

*Appended by the worker on job `t2-unblock-close` (from phone-Lena). This block is
mine and dated; §0 and everything below it is the previous workers', untouched
except where a correction is marked CORRECTED.*

**`t2` is closed.** `state/task_queue.json`, id `t2`: `blocked` → `done`, finished
2026-10-10T18:12:27, written under the daemon's own flock with a backup at
`state/task_queue.json.bak-pre-t2close-20261010-181227`. There is no `blocked` entry
left in the queue. The evidence is fresh, not quoted from a doc: from inside the
daemon's own process `DISPLAY=:0 WAYLAND_DISPLAY=wayland-0 SDL_VIDEODRIVER=unset
SDL_ASSERT=abort`, and the 2026-10-09 presenter re-run under it printed
`[ OK ] window+device+claim on driver=vulkan` … `PRESENTED 60 frames to a real
swapchain`, exit 0, no dialog, nothing left on his screen.

**P3 — the one line, and it was worth more than one line.** `LONG`/`ULONG` in
`platform/platform_types.h` were 8 bytes; they are now `int32_t`/`uint32_t`.
Measured against the i386 row as the reference: `POINT` 16→8, `RECT` 32→16, `SIZE`
16→8, `MSG` 48→40 (`hwnd` is a real 8-byte pointer, so 40 is right), and
`(LONG)(INT32_MAX+1)` wraps again. It unlocks **no** files by itself — 27/39 before
and after, same file list — and **it is an ABI change: 14 mangled symbols move**
(`Input.cpp` 12, `rhi/deck_dd7.o` 2), so every prebuilt object in the tree is
invalidated by it. That is the reason to do it now, while the tree is still small.

**P1.** The 7 pointer-bearing size asserts are re-expressed as
`sizeof(X) == (sizeof(void*) == 8 ? 0xNN : 0xNN)` — both oracles kept, nothing
deleted, the other 10 asserts untouched and re-verified as still passing. The
64-bit numbers come from clang's own record-layout dump, not from arithmetic.
**Sweep: 27 → 33 PASS, 39 → 33 FAIL, zero regressions**, and **every remaining
failure in the tree is now one class**: `cast from pointer to smaller type 'int'`
— 1,377 distinct sites across 33 files, led by `FrontendMenus.cpp` 305,
`FrontendScreens.cpp` 236, `GameInfo.cpp` 176, `StageObjects.cpp` 135.

**Verified against the port that actually runs, which is i386.** All 66 game
objects rebuilt against the new headers, `deck_dd7.o` rebuilt, platform 5/5, link
exit 0, and a 20-second windowed run compared with the pre-change binary:
**normalised logs byte-identical — 59 lines, empty diff.** The three message boxes
in that run ("Setting configuration to defaults", 2× "Make sure the CMR2 CD is in
the CD drive") appear in **both** binaries: pre-existing, GAME2-res's lane, not
caused here and not papered over.

Method, numbers, and the things deliberately left alone (`HRESULT`/`LPARAM` width;
P2 untouched): `work/P3/P3-P1-REPORT.md`. §5 items 2 and 3 are therefore done. What
is left is §5 item 1 (the pad lane) and **P2 — the only thing now between this tree
and 66/66.**

**ASK (sequencing, not a blocker).** P2 is 1,377 mechanical sites; S2 (the D3D7
state tracker) is the thing that stops the port drawing its own 3D. My plan says P2
then S2. If you want them swapped so the game looks right sooner, say so here and I
will swap — but I will not silently re-sequence my own plan.

## 1. RIGHT NOW

- **Daemon** `lena_daemon.py` pid **250660**, **2 workers**, model `deepseek-v4-flash`,
  uptime **5,400 s (~90 min)**, 400 steps per round. `state/status.json` was
  refreshed 14:37:39 — **6 s old** at the moment the relay published, so the loop is
  alive, not merely `active` under systemd.
- **Worker 1 = GAME2-res** ("Can't boot up to the native resolution on a steam
  deck"), claimed 14:29:08, **step 71** at 14:37:29. Working in `work/GAME2/r7`: it
  wrote a `mkconfig.py` to put a real `Configuration/GameInfo.rcf` where the game
  looks for it, rebuilt `outres/cmr2`, and is capturing frames
  (`R7_SNAPFRAMES=1200,1500:/tmp/final.ppm`).
- **Worker 2 = RELAY-VISIBILITY** (this page), claimed 14:34:47.
- **Queue:** 33 tasks — 18 `done`, 11 `partial`, **2 `running`**, 1 `pending`
  (`GAME2-r6-r3`, the auto-spawned continuation of the pad job), 1 `blocked` (§4).
  `state/task_queue.json`.
- **Relay:** last successful publish **14:37:25**, `first_fail: null`
  (`state/relay_health.json`). The outbox is **1,537 bytes** and now carries the
  work itself, not just the depth — §2.8.
- Load average **2.08** on 8 threads. Nothing is starved.

s — 17 `done`, 10 `partial`, **2 `running`** (those two),
  2 `pending`, 1 `blocked` (stale — §4). `state/task_queue.json`.
- **Relay:** systemd timer, 3-minute interval; last successful publish
  **14:25:19** (`state/relay_health.json`, `first_fail: null`).
- Load average 0.96 on 8 threads. Nothing is starved.

---

## 2. DONE — finished and verified

**1. Every `.bfl` container round-trips byte-identical.**
I re-ran this myself today at 14:17, from the *published* repo code, against the
retail install:

```
repack VERIFY: 581/581 byte-identical   (skipped 1 non-container)
exit 0 · 8.5 s wall (real 0m8.533s)
```

Raw output `/tmp/repack_verify.out`. The install holds 582 `.bfl` = 581 containers
+ 1 non-container, so the arithmetic closes. A modified container is therefore
structurally indistinguishable from something the game shipped.

**2. The geometry format closes exactly on every car.**
Re-ran today 14:18 through the reader in `tools/c3d.py`:
**259 / 259** car `.c3d` close on
`48 + Σ(count×stride) + TEXCOUNT×24 + TEXCOUNT×260 == filesize`,
**3,803** texture references, **zero** mismatches.

**3. The decompiled game compiles further than this repo says.**
My run, 14:19–14:20, published harness `tools/sweep-compile.sh`, one translation
unit at a time, real `.o` files, `-ferror-limit=0`:

| target | PASS | FAIL |
|---|---|---|
| `x86_64-linux-gnu` (the mandate) | **27 / 66** | 39 |
| `x86-linux-gnu` (i386 — kept as an oracle) | **64 / 66** | 2 |

Of the 39 x86_64 failures, **37 pass on i386** → blocked by pointer width and
nothing else. 2 fail on both (`Game.cpp`, `GameInfo.cpp`) and both are only
`-Wc++11-narrowing` on HRESULT literals; the local round-5 sweep passes
`-Wno-c++11-narrowing` and with that flag i386 is **66 / 66**. **Zero** files pass
on x86_64 and fail on i386, so the oracle still holds.

Error census (x86_64, *all* errors, all files): **1,561** —
1,504 `cast from pointer to smaller type` (1,449 of them to `int`),
56 size asserts over **7 distinct structs** (InputFeedbackStateSize 17,
InputDeviceStateSize 17, StageNodeTablesSize 14, PartStateSize 4,
StageArchiveSizeCheck 2, StageObjectDraw_size 1, CollisionBoxSize 1),
1 narrowing. **Zero undeclared identifiers, zero unknown types, zero incomplete
COM** — those three classes (365 errors when this was last published) are closed.

Evidence: `/tmp/sweep-verify-64/{results.txt,all_errors.txt}`,
`/tmp/sweep-verify-32/results.txt`.
**This repo is one measurement behind: README and `docs/PORT.md` still say 26/66
and 53/66.** Fixed in §5 step 2.

**4. The ported game boots, and its frontend answers the pad.**
Not "it compiles" — pixel-measured on a screen that does not move by itself
(noise floor **77 px**, bar **400 px**, counted *outside* a no-input mask):

| input | px outside mask |
|---|---|
| stick up/down, D-pad up/down, L2, R2 | 2,922 – 4,006 |
| A / START (Enter) | **7,118** |
| B / SELECT (Escape) | **7,118** |
| X, Y, L1, R1 | 0 (nothing, every screen tested) |

Six consecutive UP/DOWN presses, all above the bar, values moving:
2,922 / 3,055 / 4,006 / 4,012 / 2,985 / 2,922 — that is a selection walking a list.
Source: `work/DECKPAD/verify5-results.json` (each row carries pre/post sha1,
changed px, outside-mask px, bbox).

**And the test in the brief had to be retracted.** A raw before/after hash is *not*
valid on this build. I recomputed the no-input churn myself from the 134 raw
captures in `/tmp/recon`: with nobody touching anything and no input injected,
consecutive captures differ by **0 px on the still screens** and by
**30,388 – 32,880 px** while the attract loop animates. The "29,368 pixels for one
Down press" claim sits inside that distribution. It is not evidence; do not cite it.

**5. Analog input reaches the game in the game's own units.**
`lY = -59965` at full stick, `lZ(L2) = 56492` at 220/255; full scale `0x10000` is the
game's own `DIPROP_RANGE`, not a guess — injected 91.5% came out 91.5%. Values are
in `verify5-results.json` rows (`axis_values`).
*Provenance: measured by the pad lane; I read the JSON with my own eyes but did not
re-run the trace.*

**6. The engine reads joystick AXES as magnitudes (source).**
`Input.cpp:1577 CInput::ReadJoystick` writes raw magnitudes into
`bindings[0..7].field_0xc`; `StageObjects.cpp:12648` writes the car's
steer / throttle / brake as 0..0x3f scaled by |axis|. Throttle and brake
**default to the same axis** (axis 1) — the combined-pedal convention.
*Provenance: read out of the decomp and cross-checked against §2.5; the live-process
confirmation is the run in flig**8. The relay works end to end, and the outbox now carries the WORK, not the depth.**
I re-ran both suites after today's change, on this Deck, just now:
`state/relay_tests.sh` → **20 passed, 0 failed** (injection, locking, corrupt queue);
`state/relay_brief_tests.sh` → **22 passed, 0 failed** in the briefing section plus
**4 passed** in a new corrupt-queue section (all sandboxed to `/tmp` — they never
touch the live queue; a harness of mine leaked into the real queue once at 14:10 and
that is why the sandbox is asserted).

Live file, published 14:37:25, **1,537 bytes**:

| key | what it is now |
|---|---|
| `current` | id + first line (≤90 chars) + `running_for` + worker id, ≤3 entri**A. Deck controls (GAME2-r6 pad lane).** Round 2 (`GAME2-r6-r2`) ended
**partial** at 14:34:47 (67 steps, 1,328 s) and spawned **`GAME2-r6-r3`**, which is
`pending` as I write. Its live checkpoint is `work/GAME2-r6/CHECKPOINT.md`.
- **Round 2's headline result, in its own words:** it measured that the port already
  drives the *real* pad — `/dev/input/event10 'Microsoft X-Box 360 pad 0'`, live
  moving axis values — and read the game's own `DeviceInfo` out of `/proc/pid/mem`
  under gdb holding the same values (`lX=2097, lY=-851, lRx=-1966, lRy=4587`). It
  then found and fixed a **port** bug that had made analog impossible regardless of
  mapping: `deck_dinput.cpp`'s `prop_id()` returned the object's address, so every
  `RANGE` call returned `DIERR_UNSUPPORTED` and `controlCount` stayed 0. After the
  fix: all 8 axes answer `DI_OK`, and the game itself calls `SetProperty(RANGE)`
  ±0x10000 on each — inside an `if (controlCount > 0)`.
- **What round 2 did NOT show:** it did not re-run the masked-pixel move proof on the
  new binary ("show it MOVE with the actual pad" is *not* demonstrated by me on this
  build), and it did not confirm the post-fix read-back in game memory
  (`controlCount==8`, axis bindings 0/1/1). `m_controllerInfo` was still all zeros
  300 polls in, i.e. `RefreshControllerConfigurations` had not run — **that is the
  key unknown and it is what round 3 starts on.**
- Pad → frontend, latest measured state: **6 of 22** tested inputs register above the
  measured bar on the static screen; LEFT/RIGHT also register on other screens (both
  axes work; which one bites depends on the screen). **4 of 22 are clean negatives
  (X, Y, L1, R1) and are explained, not pending.**
- Analog **delivered**: done. Analog **consumed by the car**: **0 of 1** — the
  engine's throttle/brake axis (axis 1) is fed by nothing, because the port maps
  L2→axis 2 and R2→axis 5. The fix is port-layer (`axis1 = R2 − L2`), not written yet.

d shape
told the phone end: `next[].via` reported `hand` for a continuation the daemon
spawned itself; an empty `errors` said "nothing is wrong" while a job sat `blocked`
for 17 hours; an unreadable queue published as zero pending; and `active` from
systemd was the only health signal, which a wedged worker thread also produces.
Verified on the live wire, not only in the sandbox — the file published at 14:37:25
carries `via: "auto"` for `GAME2-r6-r3`, `blocked: [t2]`, `queue_readable: true`,
`daemon_age_s: 6`. Every number in §1 came from reading that file back.

n both the Python and the C probe.)

**8. The relay works end to end.**
Re-ran both suites today: `state/relay_tests.sh` → **20 passed, 0 failed**;
`state/relay_brief_tests.sh` → **16 passed, 0 failed** (sandboxed to `/tmp` — they
never touch the live queue). Live outbox published 14:25:19, **932 bytes**, carrying
`current` / `next` / `last_done` / `errors`.

**9. Nothing synthetic is left on his machine.**
`/proc/bus/input/devices` has **no `uinput` entry** and no leftover injected pad —
I checked at 14:18. The real nodes are there: Steam Deck Controller `event4`
(keyboard) / `event9` (mouse), and Steam Input's virtual pad
`Microsoft X-Box 360 pad 0` = `event10` / `js0`.

---

## 3. IN PROGRESS — with honest fractions

**A. Deck controls (GAME2-r6-r2) — running, step 63 / 400.**
- Pad → frontend: **6 of 22** tested inputs register above the measured bar on the
  static screen; LEFT/RIGHT also register on other screens (both axes work; which
  one bites depends on the screen). **4 of 22 are clean negatives (X, Y, L1, R1)
  and are explained, not pending.**
- Analog **delivered**: done. Analog **consumed by the car**: **0 of 1** — the
  engine's throttle/brake axis (axis 1) is fed by nothing, because the port maps
  L2→axis## 5. NEXT — the next three things, in order

1. **`GAME2-r6-r3`** — first in the queue, auto-spawned, and the pad lane's own next
   round. Its outstanding item is the one round 2 named: confirm the post-fix read-back
   in the live game (`controlCount==8`, axis bindings 0/1/1) and find out whether
   `RefreshControllerConfigurations` ever runs — if it never runs, the axis bindings
   still do not exist even with the ranges fixed. Then re-run the masked-pixel move
   proof on the new binary, and write the port-layer combined pedal axis
   (`axis1 = R2 − L2`) driven by a real analog trigger value. That converts "analog is
   delivered" into "analog is consumed".
2. **`GAME2-res`** — native resolution on the Deck panel, 1280×800, filling it with no
   letterboxing. Already running, worker 1 (§1).
3. **Then the repo's stale numbers and the source bug** — 27 / 64 (66 / 66 with the
   narrowing flag) replaces 26/66 and 53/66 in README and `docs/PORT.md`, with the
   exact command printed beside it, and `typedef long LONG` gets fixed (one line) and
   re-measured. And close `t2`, which is answered and still sitting in `blocked`.
isk
(§2.7). Nothing blocks it. **I did not edit the queue to close it** — status
changes there are the daemon's to make, and that file has already been corrupted
once by a test harness of mine.

**Genuinely blocked, with what specifically blocks each:**

- **37 files are blocked by pointer width alone** — mechanical `(int)` →
  `(intptr_t)`, ~1,449 sites, no thinking required. Plus 7 size asserts (56 sites),
  and one real bug: `platform/platform_types.h:29 typedef long LONG` is **8 bytes
  on LP64, and Win32 `LONG` is 4 bytes always**. It compiles fine and silently
  corrupts every struct holding a POINT / RECT / SIZE. Fix that line before
  trusting any layout.
- **ASAN does not build.** So **every number in this file about the port is
  compile-level**. I have no runtime evidence about the port's correctness and I
  will not imply otherwise.
- **I cannot press the physical thumb.** Pad input is injected through a
  `/dev/uinput` stand-in shaped like the real node (`event11` in that session);
  while Steam Input owns the pad, only Steam can write the real node. So "the pad
  works" means *everything downstream of the thumb is the game's own* — it does not
  mean a human thumb was used.
- **X / Y / L1 / R1 cannot work in the frontend, by the game's own design.** They
  map to DIK_SPACE / C / LBRACKET / RBRACKET, and `ReadKeyboardDevice`
  (`Input.cpp:1516`) translates exactly six keys into the 6-bit menu field. The pad
  sends them, the input layer accepts them, the frontend has no bit to put them in.
  Not a bug to patch — remapping them would change their gameplay meaning.
  **Decision for Miami, not for me.**
- **No human has looked at the game's window.** An external X11 grab of the root
  window is byte-identical with and without the game running (40 lit px,
  md5 `1cd11330e3` both ways), so every image here comes from the game's own
  readback path. *Provenance: the pad lane's measurement — I did not re-run it.*
- **The phone cannot drive this.** The relay has no shell: all it can do is append a
  queue entry, which is exactly as powerful as a job Miami types himself, and no
  more. One poll every 3 minutes, one direction per file, and the Deck never writes
  to the inbox.

---

## 5. NEXT — the next three things, in order

1. **Close the read that is running right now** (§1): get the game's own
   controller/axis table out of the live process, then write the port-layer combined
   pedal axis (`axis1 = R2 − L2`) and drive it with a real analog trigger value
   through the game's own default binding path. That converts "analog is delivered"
   into "analog is consumed".
2. **Fix the repo's numbers, then the source bug.** Re-publish the sweep table as
   27 / 64 (and 66 / 66 with the narrowing flag), plus the new error census, with
   the exact command printed beside it; fix `typedef long LONG` (one line) and
   re-measure. Delete 26/66 and 53/66 so nobody quotes them again.
3. **Then the queue's pending jobs in queue order** — RELAY-VISIBILITY step 2 is
   already built and tested (the outbox carries `current`/`next`/`last_done`/`errors`,
   §2.8), so it needs verification against the live queue rather than new code; then
   GAME2-res (native resolution on the Deck). And close `t2` as answered.

---

## 6. STILL NOT TRUE — read this one

- **The game is not ported.** What runs is the decompiled game booting to its
  **frontend**, drawing through the new SDL3/Vulkan layer. No race has been
  started, no car driven, no stage loaded. Nothing here is evidence about gameplay.
- **"It renders" is measured through the game's own readback, not a window.** No
  human has confirmed the picture. §4.
- **No pad input has ever come from a human thumb.** §4.
- **The brief's own headline measurement is invalid.** "29,368 changed pixels for one
  Down press" lies inside the no-input churn; I re-derived 30,388–32,880 px of churn
  with zero input from the raw captures myself (§2.4). Any future registration claim
  needs the no-input mask or it is unfalsifiable.
- **X / Y / L1 / R1 do not register in the frontend and cannot** (§4). If you read
  "every Deck control → the port's input path" as finished, it is four controls
  short, on purpose, pending a decision.
- **Analog steering and analog throttle are NOT live.** Analog values reach the
  game's joystick device and the engine has the code to read them as magnitudes, but
  **no axis carries them into the car** — and throttle/brake have no feeding axis at
  all right now.
- **The port has no working full backend.** 277 of the 298 D3D7 call sites are
  render-state calls and the state tracker that would service them does not exist.
  What draws today is the frontend's path, not the game's 3D pipeline.
- **Every port number here is compile-level.** ASAN does not build. §4.
- **`2.26 GB` in the README does not match this install.** I measured the retail
  install myself: 908 files, **0.59 GB** by summed file size, 610 MB by `du`;
  the 582 `.bfl` are 0.39 GB. The container count is right (581 + 1 skipped); the
  size figure is not. Not resolved, so not claimed.
- **This repo's published port numbers are stale** — 26/66, 53/66, and "27 files
  blocked by pointer width" are one measurement behind; today's are 27 / 64
  (66 with the flag) and 37 files. Docs get fixed in §5 step 2, not before.
  >> CORRECTED 18:25 (2026-10-10): the LONG line is fixed and re-measured. The
  current x86_64 numbers are **33 PASS / 33 FAIL of 66**, all 33 failures being the
  pointer-cast class. See §0.5 and `work/P3/P3-P1-REPORT.md`.
- **The queue's status field is not a status report.** It says `partial` for ten jobs
  that are finished and `blocked` for one that is not. This file is the status
  report; trust it over the queue.
  >> CORRECTED 18:25 (2026-10-10): the **blocked** one is closed now; the
  partial-for-finished-jobs half is still true and still the daemon's file.
- **Three commits were sitting on this Deck unpushed** (the dependency-map work).
  They went up at 14:30 with the previous rewrite of this file — so if you read the
  repo before then, you did not see them.
- **`blocked: [t2]` on the wire is the queue's stale entry, not a real blocker.**
  The outbox is a faithful copy of a field that is wrong (§4). I did not fix the
  queue field; the daemon owns that file.
  >> CORRECTED 18:25 (2026-10-10): the queue field is fixed — **t2 is now done**.
  See §0.5. The wire will show it on the next relay tick.
- **The queue contains a duplicate id.** `GAME2-r5` appears **twice**, both `done`
  (a hand-typed INPUT job, and a later "GO HAM ON THE GAME"). It is harmless today
  because both halves are finished, but two consequences are real: `queue_total: 33`
  is 33 *entries*, 32 distinct jobs; and the relay skips any incoming command whose
  id already exists in the queue in **any** status, so a future command that happened
  to reuse `GAME2-r5` would be dropped silently. I did not rename or delete either
  entry — the daemon owns that file. Flagging it rather than fixing it.
- **`daemon_age_s` tells you the status file is fresh, not that work is advancing.**
  A worker stuck inside one long `gdb` run keeps `status.json` ticking. `running_for`
  growing with `last_done` frozen is the pair to watch.

