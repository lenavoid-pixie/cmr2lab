# The native port

**Goal: Colin McRae Rally 2.0 (PC, 2000) compiling and running as a native
Linux binary. No Wine. No Proton. No compatibility layer.**

That constraint is the whole point. Running the 2000 Windows executable under
Proton would put someone else's translation layer between the game and the
machine — it would work, and it would be the wrong thing. The target is the
game's own code, rebuilt for the hardware it is standing on.

---

## The thing that makes this realistic

There is a decompilation of the PC build, in MSVC6 byte-matched form:

```
122 files .cpp/.h · 135,805 lines of C++
```

Named, and recognisably the game:

| | |
|---|---|
| `CarPhysics.cpp` · `Car.cpp` · `Collision2D.cpp` | the driving |
| `AIHelper.cpp` · `Race.cpp` · `StageTiming.cpp` | the rally |
| `StageObjects.cpp` (16k lines) · `RallyData.cpp` | the stages |
| `FrontendScreens.cpp` (12k) · `GameMenus.cpp` · `FrontendMenus.cpp` | the menus |
| `Graphics.cpp` · `SceneNode.cpp` · `Input.cpp` · `Game.cpp` | the frame |

**And the entire Windows surface is small** — these are re-run counts. The first
pass at this said *149 D3D7 calls / 14 Win32 calls / 5 files containing
`__asm`*, and **all three were wrong** (see `PORT-PLAN.md` §6):

```
278  Direct3D 7 member call sites (`pD3D->`), 20 distinct methods
225    of them in Graphics.cpp alone (81%)
 22  of the 278 are DRAWS; the other 256 are state
 20  DirectDraw / device-creation call sites (`pDD->`), 7 methods
 45  files that `#include <windows.h>`
 36  `__asm` occurrences, in 6 files — 0 visible to a non-MSVC compiler
```

That is the entire distance between "the game" and "the game on Linux".
Everything else — physics, damage, AI, timing, the rally itself — is already
C++ that has been reconstructed.

So the port is not *reimplementing* CMR2. The port is **compiling the real game
for native x86_64 Linux**: back 278 D3D7 call sites with SDL3_GPU, satisfy the
Win32 declarations 45 files ask for, let 36 assembly blocks compile away (they
are all inside `#ifdef _MSC_VER` with portable `#else` fallbacks), and link.

---

## Status — honest

### DONE

**M0 — it runs.** The mesh loader had a **52,560-byte heap overflow on every
single run**: the vertex pool was budgeted from `sum(part.V)` = 1,204 vertices
(43,344 bytes) and then written with `sum(part.F)*3` = 2,664 vertices
(95,904 bytes). That was the `malloc(): corrupted top size` crash. Fixed by
budgeting `sum(V)+8` vertices and `3*max(sum F, sum V)+8` indices, and
**enforcing** it with clamps rather than assuming it. Six cars now exit 0 with
geometry loaded, no core dumps.

**M1 — the format's topology is settled: indexed triangle list.** See the
topology section at the top of `docs/C3D.md`. Four independent lines, the
decisive one being the game's own decompiled loader.

Along the way a real latent bug was caught: the vertex base was 12 bytes late,
so every vertex was rendered carrying its neighbour's position. It still looked
like a car. Corrected, the vertex block reconciles byte-exact and the model is
watertight at a true 3.82 × 1.28 × 1.78 m.

### IN PROGRESS

**M2 — wheel placement.** Nodes live at `pData+0x30`: `c18` records of 396
bytes feeding the scene-node table. `SceneNode.cpp` is the reference.

### NOT STARTED

**M3 — the D3D7 → SDL3_GPU layer.** The measured surface, all of it, not just
one file: **278 call sites, 20 methods, of which 256 are state and only 22 are
draws.** The 225-call/16-method census published earlier was correct but scoped
to `Graphics.cpp` alone; the tree-wide number is the one the port has to pay.
SDL3_GPU has no `SetRenderState` (it uses immutable pipeline objects), so this
needs a **state tracker and a pipeline cache**: accumulate state, hash the
combination, cache the pipeline per combination. The interface is sketched in
`port/rhi/cmr2_rhi.h` — 105 lines, interface only, no backend — which lives in
the port working tree and is **not shipped in this repo**.

This is the real mountain. It is also the part that makes the game *a game*
rather than a renderer.

**M4 — build the decomp, count what actually compiles. COUNTED.** Re-run
2026-10-10 with `tools/sweep-compile.sh`, one translation unit at a time, real
`.o` files, `-ferror-limit=0`:

| target | pass | fail |
|---|---|---|
| `x86_64-linux-gnu` — the mandate | **33 / 66** | 33 |
| `x86-linux-gnu` (i386, kept as an oracle) | **66 / 66** | 0 |

*Re-measured 2026-10-10 18:20, after P3 + P1 (`docs/P3-P1-REPORT.md`): `LONG`/`ULONG`
became 4 bytes, and the 7 pointer-bearing size asserts were re-expressed to carry
both the 32-bit and the 64-bit layout. x86_64 went 27 → 33. Command:
`bash work/CMR2/sweep64-r5.sh <outdir>`; i386 command: `work/P3/i386/resweep-p3.sh`
(the flags `link32.sh` actually links with). The old numbers here were 26/66 and
53/66.*

**All 33 x86_64 failures are now ONE class: `cast from pointer to smaller type
'int'` — 1,377 distinct sites.** Nothing else fails: no missing type names, no COM
vtables, no negative-size asserts. Zero files pass on x86_64 and fail on i386.

### KNOWN GAPS

- **Textures: 26 of 27, and this was fixed after the note that said 0/27.** The
  `.bfl` table of contents ends in a **truncated final record** — 16 bytes where
  every other record is 24 — so the original backwards walk failed on its first
  probe and every part drew flat white. The reader now locates the table by its
  own 24-byte stride instead, and `205a1N` decodes 26 textures from its
  container. One name in the car's own texture list, `ap5unddf`, is **not in the
  container at all** and falls back to a placeholder — that is a data fact, not
  a parser bug.
- **AddressSanitizer does not build** in the port's toolchain. All evidence here
  is empirical rather than instrumented. Still the first thing worth fixing,
  because "the render looked right" is exactly the trap that hid the vertex bug.
- **Transparency changes what the render metrics mean.** With textures loaded,
  enclosed background shows through alpha parts, so a hole count is not
  comparable to one taken with flat white. Stated wherever hole counts appear.
- **The bridging metric is a dead end.** It was published as 0.00% vs 0.68% and
  does not reproduce; re-run, it reads 0.00% for both readings. Withdrawn.

---

## What exists in this repo

`src/port/` holds the native port's own renderer — C, no dependencies beyond
SDL3, written for this project:

| file | |
|---|---|
| `cmr2deck.c` | reader, SDL3/Vulkan renderer, window |
| `inflate.c` | its own gzip inflate |
| `build.sh` | zig cc → x86_64-linux-gnu, rootless |
| `CHECKPOINT.md` | the working handover, kept current |

It opens a real window, creates a real Vulkan device on a real swapchain, and
draws a model from the retail game files. **It is not a game yet** — it renders
one car. No tracks, no physics, no HUD. That is what M2–M4 are for.

---

## Legal

**No game data lives in this repository.** No models, no textures, no audio, no
executables — not one byte of Colin McRae Rally 2.0 or 3.

The reader is written against the retail files and reads them **at runtime from
a copy you already own**. That is the same rule that has kept decompilation
projects alive for decades while asset-redistributing ones die in a week:
**ship the code, let the user supply the files.**

Decompiled source, where referenced, is reconstruction of the original binary
for interoperability — not a redistribution of the game.
