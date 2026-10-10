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

**And the entire Windows surface is small:**

```
149  Direct3D 7 calls
 14  Win32 calls
  5  files containing __asm
```

That is the entire distance between "the game" and "the game on Linux".
Everything else — physics, damage, AI, timing, the rally itself — is already
C++ that has been reconstructed.

So the port is not *reimplementing* CMR2. The port is **compiling the real game
for native x86_64 Linux**: replace 149 D3D7 call sites with SDL3_GPU, stub 14
Win32 calls, handle 5 files of assembly, and link.

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

**M3 — the D3D7 → SDL3_GPU layer.** The measured surface: 16 methods, 225 call
sites, of which **173 are state, not draws**. SDL3_GPU has no `SetRenderState`
(it uses immutable pipeline objects), so this needs a **state tracker and a
pipeline cache**: accumulate state, hash the combination, cache the pipeline per
combination. The interface is sketched in `port/cmr2_rhi.h`.

This is the real mountain. It is also the part that makes the game *a game*
rather than a renderer.

**M4 — build the decomp, count what actually compiles.** An earlier count said
19/66; that was before the loader fixes and has never been re-run. The honest
number comes from running it.

### KNOWN GAPS

- **Textures: 0/27.** The `.bfl` table of contents has a **truncated final
  record** — 16 bytes where every other record is 24 — so the backwards walk
  fails on the first probe. Everything renders flat white until that is fixed.
- **AddressSanitizer does not build** in the port's toolchain. All evidence so
  far is empirical rather than instrumented. Flagged as the first thing to fix,
  because "the render looked right" is exactly the trap that hid the vertex bug.

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
