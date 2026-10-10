# S2 — WHAT THE GAME ACTUALLY ASKS THE DEVICE FOR

Round: 2026-10-10 evening, worker `seq-s2-first`. Every number here is from a run
on this Deck, in this round, from **the game's own binary** (`work/S2/build/cmr2`,
i386, linked 18:47, link exit 0) — not from a harness that imitates it.

The brief was "S2 (the state tracker) before P2". This is what I found when I
measured instead of planning, and it changes what S2 *is*.

---

## 0. THE HEADLINE, IN ONE PARAGRAPH

**The state tracker is not missing and it is not the mountain.** The port's
backend already collapses **1,334,903 draws into exactly ONE pipeline** in a
28-second run, with **zero refused draws**. What the game can reach today is the
**2D path only**: 205,714 draws, every one of them FVF `0x1c4` (screen-space
TL), while **206 mesh vertex buffers (469,890 vertices, FVF `0x2d2`) are created
and never drawn through once**. The real gap is *state fidelity*: **29,512 of the
game's 99,704 state calls in a 20-second run are answered `D3D_OK` and quietly
ignored** — 10 of the 18 render states and 10 of the 19 texture-stage states the
game names by hand have no mapping in the device. Both halves are measured below.

---

## 1. THE INSTRUMENT (this is the deliverable that makes the rest checkable)

`rhi/deck_dd7.cpp` now carries a **call census**: `DECK_DD7_CENSUS=1` turns on
counting of every D3D7 entry point the game calls, per render-state values, per
texture-stage state, per FVF, and dumps it periodically and at exit
(`DECK_DD7_CENSUS_EVERY=<frames>`). It prints the backend's own counters and
`a7_last_error` with each dump, so a refused draw cannot hide.

* patch: `work/S2/patch_census.py` (idempotent, per-file backup)
* run:   `cd ~/.lena_cmr2/game && DECK_DD7_CENSUS=1 DECK_DD7_CENSUS_EVERY=200 \
          /home/deck/lena/work/S2/build/cmr2`
* two supporting fixes found on the way, both real:
  1. `api7`'s port-side header `a7_vk32.h` declared its C functions **without
     `extern "C"`**. It had never been included from C++ before; the moment it
     was, the link failed on two symbols that exist. Fixed in the header.
  2. The device's own `draws` field counts **only** `DrawIndexedPrimitiveVB`. The
     census counts all four draw entry points. In run B the two disagree by
     exactly 77,287 — the device field said 0 because the frontend draws through
     `DrawPrimitive`. That is a stale metric in our own code, not a bug in the
     game, and it is why the census reports both.

## 2. RUN B — 20 s, and this is the whole frontend frame

```
BeginScene=1127  EndScene=1127  Clear=2255
SetRenderState=46233   SetTextureStageState=53471   SetTexture=3994
SetTransform=3382      SetViewport=2254             LightEnable=13524
SetMaterial=1          DrawPrimitive=77287          CreateSurface=142
Flip=1127              GetCaps=1                    EnumTextureFormats=1
create VB by FVF: 0x2d2 MESH = 206 buffers / 469890 vertices
draws by FVF:     0x1c4 TL = 77287        refused_by_backend = 0
device counters:  ignoredStates=29512 unmappedTexOps=0 unmappedTexArgs=0
backend: presentedFrames=1126 draws=77287 tris=159414
         pipelines=1 (built=1 hit=1125) refusedFvf=0 clamps=0
```

Run A (28 s, same binary, `work/S2/../s2run.log`): 1,607 frames, 205,714 draws,
426,806 triangles, **pipelines=1**, TL only, 0 refused. Two runs an order of
magnitude apart in draw count, the same answer: **one pipeline**.

Read the pipeline key (`A7VKPipeKey`) and this is not luck — it is
`{blend, src, dst, cull, z, zw, zf, fog, path}`, snapshotted at draw time, with
texture-stage ops carried in push constants instead. The frontend holds one
blend/z/cull configuration for the whole frame, so one pipeline is *correct*, not
a cache miss that never happened.

**The 3D path is not reached.** 206 mesh vertex buffers exist (created at load),
FVF `0x2d2`, 469,890 vertices — and `DrawIndexedPrimitive`/`DrawPrimitiveVB` on
them: zero. `SetMaterial` is called exactly once in the whole run. So
`Graphics.cpp`'s 3D pipeline is set up and never driven, which is what the port
plan said in words ("what draws today is the frontend's path") and is now a
number.

## 3. THE STATE CENSUS, FROM THE GAME'S OWN SOURCE — 278 AND 298 RECONCILED

`tools/d3d_census.py` (one run, no arguments needed) counts D3D7 call sites by
**the last hop of the receiver chain**, which is the only definition under which
the published figures mean anything:

```
scope                    sites  methods  draws  state(=sites-draws)
pD3D-> only                278       20     22    256
pD3D-> + pDD->             298       26     22    276
```

* **278 / 20 / 256 / 22 is what the README publishes, and it is correct.** It is
  the `pD3D->` — that is, `CGraphics::m_pTextureManager->pD3D` — scope, the
  device the game draws through.
* **298 is the same census with `pDD->` (the `IDirect3D7` object) added** — so
  **276** state, not 277. The `277` in `progress.md` §6 (and in the old PORT-PLAN
  line) is `298 − 21`: it counts 21 draws and there are **22** (it misses
  `pD3D->DrawIndexedPrimitive`, 1 site). Off by one, and now marked.
* `518` (the old "all typed receivers" figure) I could **not** reproduce: a
  method-whitelisted count gives 400, a count of every `x->Method(` in the tree
  gives 533. Those are three different definitions and I am not going to publish
  a fourth; **518 should be quoted only with its scope, or dropped.**

The count is not comment text: stripping `//` and `/* */` first gives the same
278/20/298. It also survives the mistake that matters — a regex that stops at the
first identifier matches **275**, silently losing every call written
`m_pTextureManager->pD3D->Method(...)`. That is a 3-site error that would have
looked like a tree change.

## 4. THE GAP: WHAT THE PORT DROPS ON THE FLOOR

Same tool, second half. Every render state and texture-stage state named in the
game's source, against `map_rs` / `map_tss` in `deck_dd7.cpp`:

| dropped render state | sites | the function that sets it |
|---|---|---|
| `D3DRENDERSTATE_COLORVERTEX` | 5 | `Graphics_SetLightingMode` |
| `D3DRENDERSTATE_DIFFUSEMATERIALSOURCE` | 5 | `Graphics_SetLightingMode` |
| `D3DRENDERSTATE_CLIPPING` | 4 | `Sprite_DrawLayer`, `ScreenLine2D_Draw` |
| `D3DRENDERSTATE_FOGTABLEMODE` / `FOGVERTEXMODE` | 3 / 3 | `Graphics_SetFog`, `EnableFog`, `DisableFog` |
| `D3DRENDERSTATE_FOGCOLOR` / `FOGSTART` / `FOGEND` | 1 / 1 / 1 | `Graphics_SetFog`, `Graphics_EnableFog` |
| `D3DRENDERSTATE_NORMALIZENORMALS`, `LOCALVIEWER` | 2 / 2 | `Mesh_DrawEnvMapped` |
| `D3DRENDERSTATE_TEXTUREFACTOR` | 1 | `Graphics_SetTextureFactorAlpha` |

Texture stage: `TEXCOORDINDEX`, `TEXTURETRANSFORMFLAGS`, `MIPFILTER`,
`MAXMIPLEVEL`, and the five `BUMPENVMAT*`/`BUMPENVL*` — 10 of 19 named states,
same treatment.

A dropped state is **not a crash**: `deck_dd7.cpp` returns `D3D_OK` and counts it
in `ignoredStates`, because a stub that refuses is worse than one that lies about
*why*. The measurement says this is not hypothetical: **29,512 silently ignored
calls in 20 seconds**, including `D3DRENDERSTATE_COLORVERTEX` (45,009 calls in
run A) and `D3DRENDERSTATE_LIGHTING` — the states a lit mesh path is made of.
For the 2D TL path most of them do not change a pixel. For a car on a stage they
are the difference between shaded and flat.

## 5. THE VERDICT ON THE "SMALLER FIRST MILESTONE"

The phone's proposal: *pick the handful of state combinations the 3D pipeline
uses on a stage, hardcode a pipeline per combination, get ONE real 3D frame,
then generalise into the tracker proper.*

**The instinct is right; the mechanism is already built and the lever is the
wrong one.** Measured reasons:

1. **There is no pipeline-per-combination work to hardcode.** 1,334,903 draws
   produced one pipeline; the cache keys on a 9-field struct built from live
   state at draw time, and (per the comment in `a7_vk32.c`) it used to be
   hash-based and silently baked in the frame's last state — that bug is already
   fixed and the fix is the structural key. Writing a hardcoded table now would be
   *deleting* a general solution to re-add a special case.
2. **The 3D frame cannot be seen yet, and that is not an RHI problem.** Nothing on
   the reachable path issues a mesh draw. I drove the game from the attract loop
   with the pad (12 presses: A, UP+A) and the census never showed one mesh draw.
   Reaching a race is a *game-state* problem — menu navigation, the CD/boot-video
   path, session setup — not a tracker problem.
3. **What would actually be wrong, and can be measured today, is the dropped
   state.** That is S2's honest content, and it has a crisp target: drive
   `ignoredStates` to 0 for the states the mesh path uses, and prove each
   addition against the census rather than against a screenshot.

So S2 splits:

* **S2a — state fidelity.** Implement the dropped states the 3D path needs
   (`COLORVERTEX` + the material sources, fog, `NORMALIZENORMALS`,
   `LOCALVIEWER`, `TEXTUREFACTOR`; `TEXCOORDINDEX`, `TEXTURETRANSFORMFLAGS`,
   `MIPFILTER`, `MAXMIPLEVEL`, bump). Measurable now, with no race required:
   the census prints `ignoredStates` per run.
* **S2b — reach a race.** Get the game to a track so the mesh path runs at all.
   This is where the "one real 3D frame" lives, and it is the same blocked
   problem the frontend work has been circling.

## 6. HONEST LIMITS

1. **The mesh-path state set is read from source, not measured at runtime** —
   because no run reached a mesh draw. §4's "the function that sets it" column is
   the evidence for *who* sets each state; it is not evidence that the state
   arrives in the order the mesh path expects.
2. The census is **process-global**; a second device in one process would share
   counters. The game creates one.
3. The **signal handler uses `fprintf`**, which is not async-signal-safe. It is
   diagnostic-only, and it is why a `timeout -s INT` run still reports.
4. `pipelines=1` means one *distinct key among the draws made*. It does not by
   itself prove the picture is right — only that the cache is not thrashing.
5. The 12-press drive did **not** reach a race and I did not keep pushing at it
   this round; the trigger-capture path (`A7VK_SNAP_TRIGGER`) only wrote frames
   after ~100 s in, which is itself worth a look next round.
6. Nothing here is a claim about frame *rate* or *residency*: 1.33 M draws in 28 s
   is 4.5 k draws/s of 2D, which says the loop runs and says nothing about
   whether the frontend is drawn the way the original did.

## 7. FILES

| what | where |
|---|---|
| the census patch | `work/S2/patch_census.py` |
| the census tool (both halves) | `work/S2/d3d_census.py` → published as `tools/d3d_census.py` |
| the drive attempt | `work/S2/s2_drive.py` |
| raw runs | `/tmp/s2run.log` (28 s), `/tmp/s2run2.log` (20 s), `/tmp/s2-drive.log` |
| game binary used | `work/S2/build/cmr2` (i386, link exit 0) |
| device source | `~/.lena_cmr2/port/rhi/deck_dd7.cpp` (+ `.bak-pre-census-*`) |
