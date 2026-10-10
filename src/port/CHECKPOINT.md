<!-- Published copy. Local absolute paths in the original working notes have
     been replaced with placeholders: $CMR2_HOME is where the game files, the
     decomp source tree and the port scaffolding live; $PORT is this directory;
     $TOOLCHAIN is a rootless zig/cmake/ninja install. Nothing else was edited,
     and the numbers are the ones measured in the round dated below. -->

# CMR2 ON THE DECK — CHECKPOINT

Last written: 2026-10-10, from the Deck. **M0 and M1 are DONE.**
Next unfinished milestone: **M2 (wheel placement)**.

**What "DONE" means here, stated so it cannot be over-read.** M0 = the viewer
loads a car and exits 0 with the capacity it printed, verified this round on
**6 of the 259 cars** — not all of them. M1 = the format's topology is settled,
by the game's own loader, and every number in `docs/C3D.md` was re-run before it
was committed. It does **not** mean the loader is verified across the fleet:
of those 6 cars, 4 give a car-length bbox (3.81–3.82 m) and 2 (`205a5`,
`205c5`) give 1.76 m because their part lists are interior meshes — a cockpit,
not a body — which is an untested hypothesis, not a finding.

If you are a version of me with no memory: read this file top to bottom, then run
`./build.sh && ./build/cmr2deck 205a1N --game $CMR2_HOME --shot /tmp/a.bmp`.
It should print a `[MESH]` line, a 3.82 x 1.28 x 1.78 m bbox, and exit 0.

---

## 0. HOW TO BUILD AND RUN

```sh
cd $PORT
./build.sh                 # zig cc, x86_64-linux-gnu -> build/cmr2deck
./build.sh asan            # (BROKEN, see "dead ends")
./build/c3dprobe  FILE.c3d # per-part dump, index sanity, raw record hexdump
./build/toposcan  DIR      # brute-force offset/stride scan over every car
./build/manifold  DIR      # edge-sharing histogram, both readings
MESH=strip YAW=218 ./build/cmr2deck 205a1N --game ... --shot /tmp/s.bmp
python3 tools/compare_bmp.py /tmp/a.bmp     # coverage/holes/comps + ASCII art
```

Toolchain: `. $TOOLCHAIN/env.sh` (zig + cmake + ninja, rootless).
**`build.sh` pins `ZIG_GLOBAL_CACHE_DIR=$TOOLCHAIN/zcache-local`.**
The shared `~/.cache/zig` got pruned and `zig` then fails to link with
"cannot open .../crt1.o". Do not remove that export.

Shaders `car.vert.spv` / `car.frag.spv` must sit **next to the binary**
(`build/`). Originals are in `$CMR2_HOME/port/sdl3_scene/`; they are copies,
not generated — there is no glslc on the host (there is one in the lenabuild
distrobox if you ever need to recompile them).

Env knobs on cmr2deck: `CMR2_GAME`, `MESH=indexed|strip`, `YAW`/`ELEV`/`DIST`
(fixed camera for reproducible shots), `WX`/`WY`/`WZ`, `NOWHEELPLACE=1`, `SPIN=1`.
Args: `[car] --game DIR --shot FILE --list --mesh indexed|strip`.

---

## 1. M0 — MAKE IT RUN. **DONE.**

### The heap corruption
`c3d_load` sized the vertex pool from `sum(part.V)` = **1204 vertices**
(43344 bytes) and then wrote **`sum(part.F)*3` = 2664 vertices** (95904 bytes)
into it. A **52560-byte overflow** past the end of a `malloc`, every run,
which is exactly the glibc `malloc(): corrupted top size` + core dump.

Cause: the old code duplicated vertices per face (3 fresh vertices per triangle)
while budgeting one vertex per file vertex. Neither topology actually needs more
than `sum(V)` — INDEXED shares vertices, STRIP consumes each once.

Fix: capacity is `sum(V)+8` vertices and `3*max(sum F, sum V)+8` indices, and it
is **enforced** with clamps, not assumed. `P->vstart` now records where each
part's vertices begin so both readings index into one shared pool.

Note the index budget must be `max(sum F, sum V)`, **not** `sum F`: the strip
reading needs 3*sum(V-2) which is 46% bigger, and budgeting `sum F` silently
truncated the strip control (icap 2670/2672, 6 parts left empty) and made the
first M1 comparison worthless. Caught it; fixed it; re-ran.

Also added: `calloc` instead of `malloc`, an absurd-count guard, a hard check
that the block layout does not run past the payload, and a `[WARN]` when a part's
V overruns the vertex block.

### `--list` ignoring `--game`
`--list` was handled *inside* the argv loop and returned immediately, so it only
ever saw a `--game` that happened to come earlier in the command line
(`cmr2deck --list --game DIR` silently listed nothing). Parsing and acting are
now separated: `--list` sets a flag, the flag is consumed after the loop, once
the game dir has been resolved. Both orderings now agree.
Verified: `--list --game DIR` → 259 geometry files.

### Evidence
`205a1N` and five other cars: **exit 0**, geometry loaded, `[MESH]` reported,
BMP written. No sanitizer available (see dead ends), so the proof is the
absence of the abort plus the reconciled capacity figures printed every run:
`vcap=1204/1212 icap=2664/3620`.

---

## 2. THE `.c3d` LAYOUT — GROUND TRUTH, VERIFIED

**The game's own loader exists in the decomp tree.**
`$CMR2_HOME/port/tree/CMR2Decomp/Sector.cpp`, function
`Sector_RelocateStageMeshFile` (CMR2 `0x004b93c0`), is the PP_F reader. Every
offset below is its arithmetic, and it now reconciles byte-exact.

```
48                        pNodes        = pData + 0x30
+ c18 * 396               pMeshArray    (nodeCount, 0x18c each — SCENE NODES)
+ (c1a+c1c) * 288         pStageObjects (meshCount + objectCount, 0x120 each)
+ c1c * 160               pSectors
+ c1e * 136 + c28 * 28    pTriangles    (0x88 each, then 0x1c MeshPart records)
+ c20 * 76                pVertexData   (triangleCount, 0x4c MeshTriangle each)
+ c14 * 48                pLightLevels  (vertexCount, 0x30 FVF vertex each)
+ c14 * 4                 pVertexFlags
+ c10 * 20                pRecords
+ c26 * 92                pEnd
                          textureRecords = *(int *)(pData + 0xc) + pData
```

Check on `205a1N.c3d` (payload 165636 bytes):

```
48 + 15*396 + (14+0)*288 + 0*160 + 0*136 + 0*28 + 888*76
   + 1204*48 + 1204*4 + 888*20 + 1*92              = 157968
157968 + 27*24 + 27*260                             = 165636  == payload  EXACT
```

### The trap that cost a session — READ THIS
The old code used `VBLK = C14 + 12` and read **position at +36**. That is
algebraically **the same byte as the next vertex's position**, and I had the
vertex field order wrong too. Result: every vertex was drawn carrying its
*neighbour's* coordinates, with normal/colour/uv read out of the wrong fields.
It still produced a car-shaped silhouette and a plausible-looking bbox, which is
why it survived.

Proven by dumping both bases on `205a1N`:

| base | field @+12 read as floats | verdict |
|---|---|---|
| `77508` (loader `pVertexData`) | `(0, 0, -1.000)` — unit length; diffuse word `0xffffffff` | **correct** |
| `77520` (old `C14+12`) | NaN / `0.5` / garbage | wrong |

Correct vertex record (D3D7 FVF, 0x30 = 48 bytes):

```
+0   position  3 x float32
+12  normal    3 x float32
+24  diffuse   DWORD        (0xffffffff = opaque white on every vertex sampled)
+28  specular  DWORD
+32  uv set 0  2 x float32
+40  uv set 1  2 x float32
```

Correct offsets after the fix, and the bbox it produces for `205a1N`:
**x 3.82 m (length), y 1.28 m (height), z 1.78 m (width)** — a real Peugeot 205
is 3.71 x 1.38 x 1.57. Before the fix the bbox was 3.82 x 1.28 x 1.76 but the
numbers were coming from the wrong vertices entirely.

### Mesh record (0x120, at pMeshArray + i*0x120) — this is what I called "the part record"
```
+0x00  name[12]
+0x0c  vertex byte offset      (relative to pVertexData; == verts_before * 48)
+0x10  vertex count
+0x24  triangle byte offset    (relative to pTriangles; == tris_before * 76)
+0x28  triangle count
+0x38  MeshPart *pParts[50]    (runtime pointers; the 0x1c records live in the c28 block)
+0x100 partCount
```
`facoff == faces_before * 76` held for **2075/2075** parts across all 259 files,
derived from the part table alone — that is what pins the 76-byte stride without
ever touching the header.

### MeshTriangle (0x4c = 76 bytes)
```
+0x00  u16 flags                 material groups (0xffff = unset)
+0x04  int textureIndex[10]      ten per-triangle texture slots; the loader adds
                                 m_textureCount to every entry that is not -1
+0x2c  int field_0x2c            -1 in file; set at runtime per group
+0x30  int field_0x30            -1 in file; set at runtime by Mesh_BuildParts
+0x34  BYTE colour[3][4]
+0x40  u16 vertexIndex[3]        <-- THE INDICES
+0x46  BYTE[6]
```

---

## 3. M1 — INDEX QUESTION. **SETTLED: INDEXED. The phone-side "triangle strip" note is WRONG.**

Four independent lines of evidence. Do not re-litigate this.

### 3a. The game's own code (ground truth, decisive)
`CMR2Decomp/Graphics.cpp:3209-3246`, twice:

```c
for (i = 0; i < triangleCount; i++) {
    textureIndex = pMesh->pTriangles[i].field_0x30;
    ...
    g_unk0x006dd9bc[count++] = pMesh->pTriangles[i].vertexIndex[0];
    g_unk0x006dd9bc[count++] = pMesh->pTriangles[i].vertexIndex[1];
    g_unk0x006dd9bc[count++] = pMesh->pTriangles[i].vertexIndex[2];
}
... ->DrawIndexedPrimitiveVB(D3DPT_TRIANGLELIST, ..., g_unk0x006dd9bc, count, 0);
```

It builds an **index buffer** from `vertexIndex[0..2]` and draws
**D3DPT_TRIANGLELIST**. There is an index buffer. It is a triangle list.

### 3b. Statistical (`tools/toposcan.c`, all 259 cars, 2075 parts, 119753 records)
Scanning every even offset inside the 76-byte record for a u16 triple that is
in `[0, part.V)` with no repeat:

```
+52  99.968%   <-- winner (119715 of 119753 records)
+54  94.663%   (same triple, one u16 late — overlapping shift)
+56  96.645%   ( " )
+68  24.722%
+64   2.731%
+44   0.000%   ... every other offset 0.000%
impossible offsets: +0 +2 +6 +10 +14 ... +50 +58 +62 +66 +70    (all  0.000%)
```

`+52` relative to my old `U9 = C20+12` base is byte `C20+64` — i.e. 0x40,
`MeshTriangle::vertexIndex[3]`, exactly where the decomp says it is. The offset
scan and the decomp agree; my old notation was just 12 bytes off.

For a wheel (V=86) three *unrelated* u16 land inside 0..85 about once in 4.4e8.
Expected accidental hits over 119753 records ≈ 0.0003. Observed: 119715.
This is not a coincidence and it is not a heuristic.

### 3c. Triangle count
```
sum of part.F (what the file says)   = 119753
INDEXED reading produces             = 119753   (exact, by construction)
STRIP reading produces sum(V-2)      = 175173   (+46.3%)
```
`sum(part.F) == header c20` in 254/259 files. Under the strip reading the entire
67 KB triangle block on `205a1N` (888 records x 76 bytes) is dead weight that the
loader nonetheless walks, and whose records nonetheless contain valid indices.

### 3d. Rendering both (`--mesh indexed|strip`, four angles, `tools/compare_bmp.py`)
```
read      yaw   cover%   holes  comps  peri/area
indexed    38   11.93%       0      1      0.017
indexed   128   11.75%       0      1      0.018
indexed   218   12.98%       0      1      0.016
indexed   308    9.37%       0      2      0.021
strip      38   11.94%     120      4      0.019
strip     128   11.83%     320      3      0.020
strip     218   13.15%     337      1      0.018
strip     308    9.37%     245      2      0.025
```
`holes` = background pixels not reachable from the border, i.e. actual missing
surface enclosed by geometry. The indexed mesh is **watertight at all four
angles**; the strip leaves 120-337 hole pixels and splits the model into up to
4 components. In-engine 3D bridging (triangles longer than 5x the part's median
edge): indexed **0.00%**, strip 0.68%.

### 3e. Honesty about 3d
**The silhouettes for the two readings are nearly identical** (see the ASCII art
in `python3 tools/compare_bmp.py`). At 1280x720 the hole difference is ~0.3% of
covered pixels. A render alone would NOT have settled this — 3a is what settles
it, and 3b/3c are what make it airtight. Recorded so nobody repeats the
"just eyeball it" mistake.

### DEAD END — do not repeat
I tried **edge manifoldness** as the discriminator (`tools/manifold.c`): build
the edge histogram and compare how many edges are shared exactly twice.
It does **not** work. A triangle strip generated from vertex order is manifold
*by construction* (consecutive triangles share an edge, `V-E+F = 1` for every
part, zero non-manifold edges), so it scores *better* than the real mesh
(indexed: 34.7% of edges shared twice, 197 edges shared 3+; strip: 49.1%, zero).
A cleaner-looking manifold test here is a trap, not evidence. The tool is kept
because the edge histogram is still useful for spotting a genuine topology bug.

---

## 4. M2 — WHEEL PLACEMENT. **NEXT. Not started.**

Renders still place wheels by inference (`WX/WY/WZ`, `NOWHEELPLACE=1`), which is
a fudge. The lead is now confirmed as correct by the loader:

> **`pNodes = pData + 0x30`, `nodeCount = c18`, stride `0x18c` = 396 bytes.**
> These are the **scene nodes** (`pNodes` feeds `Graphics_AccumulateCounterOrInitialize`
> and the scene-node table). `205a1N` has 15 of them, and the header's packed type
> pairs `0x000E000F` / `0x00020003` point here (14 meshes + 1, and 2 + 3).

So: decode the node record, find the per-node transform/translation, map node ->
mesh, and place the four `01WhlLOD00` meshes from the node translations instead of
mirroring signs. `CMR2Decomp/SceneNode.cpp` and `SceneNode.h` are in the tree and
are the place to read next — `SceneNode.cpp:1487` does
`pMesh->pTriangles = (MeshHeader[3])`, i.e. the node holds the mesh pointers.

Started this session, not finished: `port/inspect_c18.py` and `port/c18.py` exist
from earlier and dump the block, but nothing has been decoded from them yet.

---

## 5. KNOWN ISSUES (not milestones, but real)

1. **Textures — CORRECTED 2026-10-10. The old diagnosis was only part of it.**
   The `.bfl` is **gzip -> CMPR** and its TOC is a run of 24-byte records at the
   end of the payload with a truncated final record; the TOC is now located by
   its own 24-byte stride and `205a1.bfl` parses (26 of 27 names decode).

   **But that is the DDS case only.** Measured over all 220 `.bfl` in
   `Game/Cars`: **44 hold `.dds`, 176 hold `.tga`.** `bfl_parse` scores a
   candidate TOC by validating block magics, and the scorer only accepts
   `"DDS "` and `"-XFILE."`:
   ```c
   if (!memcmp(p + off, "DDS ", 4)) val++; ...
   ```
   A `.tga` container validates nothing, scores 0, and is rejected whole:
   `[WARN] no usable .bfl ... -- drawing untextured`. Confirmed on bytes —
   `205a1.bfl`'s first block starts `DDS `, `205c1.bfl`'s starts `00 00 02 00`
   (uncompressed TGA header). `tools/bfl-read.py` parses all 220 including the
   TGA ones, which is how it was isolated. **Fix: score TGA blocks too, and add
   a TGA decode path.** Do not chase the truncated record again — it was fixed
   and it was never the main cause.
2. **`--list` says 259, `toposcan` sees 254.** Five `.c3d` files have
   `sum(part.F) != header c20`. Probably stage/other files, not cars. Unchecked.
3. **`c1c` is used twice in the loader** (`(c1a+c1c)*0x120` and `+c1c*0xa0`),
   which is probably a decompilation artefact. Irrelevant for cars (c1c=0),
   will matter for stage meshes.
4. **Per-part texture binding is unopened.** The `c28` block is an array of
   `MeshPart` records (0x1c = 28 bytes: `texture`@0, `minIndex`@8, `maxIndex`@0xc,
   `indexCount`@0x10, `pData`@0x14) and `Mesh::pParts[50]` at +0x38 points into
   it. That is the real answer to "PER-PART TEXTURE ID", which is currently done
   by fuzzy-matching part names against texture names in `pick_tex()`. Also,
   each `MeshTriangle` carries **ten** per-triangle texture slots at +0x04.
   Both are better than `pick_tex` and should replace it.
5. Textures in `.bfl` are DXT5; `dds_decode` handles DXT5 and raw 32bpp.

---

## 6. M3 — THE RHI. Not started.

`$CMR2_HOME/port/cmr2_rhi.h`, 105 lines, interface only, no backend.
225 D3D7 call sites, 16 methods, 173 of them state rather than draws. SDL3 GPU
has no `SetRenderState`, so this needs a state tracker + pipeline cache.
`port/RHI-MEASUREMENT.md` has the call-site census. **The real mountain.**

Useful now: `CMR2Decomp/Graphics.cpp` has the live render path, and the state
that is actually set per-mesh is small and visible — `SetRenderState` for
`NORMALIZENORMALS`, `LOCALVIEWER`, alpha blend/test, texture stage changes,
`SetTextureAddressClamp(0/1)`, and per-mesh texture switching *mid-mesh* by
`field_0x30`. Note the two texture stages and the two UV sets in the vertex
format — this game is doing multitexturing, and the RHI must not assume one.

---

## 7. M4 — COUNT THE SOURCES. Not started.

"19/66 compile" was pre-fix and was never re-counted. **There is a compiler now**
(zig on the host; gcc 16.2.1 inside the `lenabuild` distrobox via
`$TOOLCHAIN/lenacc.sh`). Source tree is
`$CMR2_HOME/port/tree/CMR2Decomp` (133 files) plus `port/tree/third_party/
dx7sdk-7001`. Not counted yet.

---

## 8. DEAD ENDS — DO NOT REPEAT

- **ASAN does not build.** `zig cc -fsanitize=address` links but dies on
  `undefined symbol: __asan_unregister_elf_globals` (bundled compiler-rt is
  older than the generated instrumentation). The distrobox gcc wants
  `/usr/lib64/libasan.so.8.0.0`, which exists only on the host at
  `/run/host/usr/lib64` and is not reachable as that absolute path from inside
  the container. `build.sh asan` is therefore non-functional.
  **Consequence: all correctness evidence here is empirical, and that is a real
  gap.** First thing to fix if you want better tooling — a working sanitizer
  would have found the 52 KB overflow in seconds.
- **`~/.cache/zig` is pruned and breaks linking.** Use `zcache-local`.
- **Every prebuilt artifact under `sdl3/build*` and `port/src/ortho3|viewer` is
  ARM aarch64** — built on the phone. There are zero usable x86_64 artifacts in
  the tree except what `build.sh` produces. Use the system SDL3
  (`/usr/lib64/libSDL3.so.0.4.12`, has wayland+x11) with the 3.4.18 headers.
- **Manifoldness is not a valid discriminator for indexed-vs-strip.** See 3e.
- Do not trust the old header comment in `cmr2deck.c` claiming "position @ +36,
  colour @ +0, uv @ +24, verified against five rival layouts". It was verified
  against five rival layouts *on top of* a vertex base that was already 12 bytes
  wrong, so it validated the wrong bytes and produced a false positive. The
  corrected layout is in section 2.

---

## 9. FILE MAP

```
cmr2deck/
  build.sh                 build (zig). pins ZIG_GLOBAL_CACHE_DIR
  build-asan.sh            dead end, see section 8
  src/cmr2deck.c           the viewer: C3D reader, BFL, DXT5, SDL3 GPU, both meshes
  src/inflate.c            own gzip inflate (SteamOS has no zlib.h)
  tools/c3dprobe.c         per-part dump + index sanity + raw record hexdump
  tools/toposcan.c         brute-force offset/stride scan over all cars   <- M1 3b
  tools/manifold.c         edge histogram, both readings (dead end for M1, 3e)
  tools/compare_bmp.py     coverage / holes / components / ASCII art of a render
  tools/patch_m0m1.py      the one-shot surgery that made the M0+M1 changes
  build/                   binary + the two .spv shaders (shaders must be here)
  src/*.bak-*              pre-patch snapshots, keep

game data:  $CMR2_HOME/game/Game/Cars/    259 .c3d, 582 .bfl
decomp:     $CMR2_HOME/port/tree/CMR2Decomp/   Sector.cpp, Graphics.cpp, Mesh.h,
                                                 SceneNode.*  <- READ THESE FIRST
originals:  CMR2.exe is PE32 i386, 1.24 MB, the decomp reference
```

---

# 10. NEW DIRECTION — NATIVE PORT IS THE PLAN. PROTON IS CLOSED. (appended, not rewritten)

**Added after the M4 census. Full detail in [`docs/PORT-PLAN.md`](../docs/PORT-PLAN.md) — read that
first, this is the summary and the redirect.**

## The redirect, stated once
the project owner's call, and it is correct: **native x86_64 Linux, compiling the ACTUAL
game.** Not "reimplement CMR2 in C". Not the 2002 binary under Proton — that is
the diminished version and it is **closed, not deferred**. Verified there is no
thread to unwind: `grep -ri proton $CMR2_HOME/port/*.md` = **0 hits**, wine is
not installed and is in no step below. Do not re-open it from the "it would be
easier" direction.

`cmr2deck` is not a rival to the decomp tree — **it is the BACKEND.** Its
Vulkan/SDL3 renderer, c3d loader and window are the D3D7 replacement layer the
decomp links against. The two halves were always meant to meet. **The spinning
window stays up** — that is native work and it is ours.

## THE NUMBER THAT CHANGES EVERYTHING: the decomp is REAL
§1.2 of the plan. **3,364 functions marked; 3,344 (99.4%) have a decompiled body.
16 are genuine placeholders.** The "463 stubs" I reported first was my classifier
calling one-line accessors stubs — `GetGameLanguage { return m_gameInfo.field_0x14 & 7; }`
is the decompiled implementation, not a placeholder. **Stubs are not the problem.
The tree is a real decomp and it compiles toward a port, not a rewrite.**

## §7 "19/66" IS NOW COUNTED. Twice. On two targets.
Replaces the un-recounted figure. Same tree, same shims, `sweep10.sh`:

| target | PASS | FAIL |
|---|---|---|
| `x86_64-linux-gnu` (the mandate) | **26** | 40 |
| `x86-linux-gnu` (i386 — what sweep9 was accidentally building) | **53** | **13** |

The two targets fail for **entirely different reasons**, and the 27-file gap is
**purely the 32→64-bit ABI cost**. (Re-run 2026-10-10 in `sweep11`; the 44/22
figure this file first carried was from sweep9, whose harness differed. 53/13 is
what the published `tools/sweep-compile.sh` produces today.) i386 failures are all shim gaps (REFGUID,
WAVEFORMATEX, ShowCursor); x86_64 failures are size asserts and pointer→int casts.
**18 files are blocked only by pointer width.** That is the price of the mandate,
and it is bounded and mechanical.

**1,751 errors in 5 classes** (x86_64, `-ferror-limit=0`): ptr→int casts 1,258 ·
undeclared identifiers 181 · unknown type names 120 (14 distinct) · incomplete COM
~119 · **size asserts 54 (only 7 distinct structs)** · DrawPrimitiveVB 2.

Collapses: **22 files failing on "negative array size" is ONE bug — 7 structs**, of
which 5 live in headers. Re-counted from the compiler: 54 errors, 7 distinct
asserts (InputFeedbackStateSize 17, InputDeviceStateSize 17, StageNodeTablesSize
12, PartStateSize 4, StageArchiveSizeCheck 2, StageObjectDraw_size 1,
CollisionBoxSize 1). Error census 1,751: ptr->int 1,300, undeclared 181, unknown
type 120, incomplete COM 65, negative array 54, other 31. And 11 of the 18 size asserts are pointer-free and PASS —
**keep those, they are a free correctness oracle.**

## §8 DEAD END UPDATE — __asm was never a blocker
**0 of 35 `__asm` blocks are visible to a non-MSVC compiler.** All 35 are inside
`#ifdef _MSC_VER` with real portable `#else` fallbacks. `FixedPoint.h` (26 files
include it) is already ported to `long long`. **Stop worrying about asm; it is done.**
Two live caveats instead: `fistp` rounds-to-nearest but the fallback `(int)` cast
truncates (5 sites) — fix with `nearbyintf`; and fixed-point intrinsics are 0.

## NEW BUG FOUND, FIX BEFORE IT BITES
`platform/platform_types.h:29` — `typedef long LONG;` is **8 bytes on x86_64 LP64.
Win32 LONG is 4 bytes, always.** The header's comment "Win32 LP64->our LP64" is a
category error (Win32 is LLP64). Verified `sizeof(LONG)==8`. Compiles fine, silently
corrupts `POINT`/`RECT`/`SIZE` and every struct holding a LONG. Fix:
`typedef int32_t LONG; typedef uint32_t ULONG;` — one line, do it first.

## What replaced M4, and what M4's real answer was
M4 was "count the sources". **Answered: 122 files, 135,805 lines — matches the
brief exactly.** M4 becomes M4a (the 64-bit source pass), and the milestones now read:

- **M4a** P3 (`LONG`) + A/D/Z shim gaps (14 typedefs, 6 COM vtables, DrawPrimitiveVB)
  → expect 26 → ~38–42 on x86_64
- **M5** S1+S3 — window (SDL3) + message loop + file/time shims. `main.cpp` links.
- **M6** P1+P2 — 7 size asserts + ~1,466 `(int)`→`(intptr_t)` sites (≈900 in 4 files)
  → expect ~60/66. **This is the mandate's cost.**
- **M3 (unchanged, still the mountain)** S2 RHI backend — 277 of 298 D3D7 calls are
  STATE; SDL3 GPU has no SetRenderState, so state tracker + pipeline cache.
- **M7** THE FIRST FRAME: `WinMain` → `CMain::Initialize` → `Game_DrawSceneViewport`
  (`0x0049d3f0`, Game.cpp:2580) → `BeginScene` … `EndScene`, **real CMR2 code**.

Entry path verified from the tree, not assumed: `WinMain`(main.cpp:52) →
`CMain::Initialize`(0x004a9720) → `CreateGameWindow`(main.cpp:137) → message pump →
`CMain::MessageHandler`(main.cpp:186) → `CGame::RunStateRenderCallbacks`(Game.cpp:1147)
→ `Game_DrawSceneViewport`(0x0049d3f0). Windowed is a first-class path
(`isFullscreen` only changes style) — use it for the first frame, do not disturb
his desktop mode.

## HONESTY LEDGER (mine, this round)
Reported 463 stubs (wrong → 16). D3D calls 5 (regex bug → 298 call sites/21 draws).
Error tally 493 (clang truncated 14 files → 1,751). windows.h 26 files (missing
`re.M` → 46). Full list in plan §6. **ASAN still does not build, so every number
here is compile-level, not runtime — do not imply otherwise.**

## KEEP
Include order `PLAT,ROOT,SHIM` is load-bearing (SDK-first = 0/66). Use
`zcache-local`. Do not touch the `.vcxproj`. Keep the 10 pointer-free size asserts.
The i386 sweep stays as a **diagnostic oracle**: fails on x86_64 but passes on
i386 ⇒ the cause is pointer width, full stop.
