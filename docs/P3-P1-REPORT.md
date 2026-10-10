# P3 + P1 — the 32-bit type layer, and the layout oracles

Worker: the one that was sent to close `t2` and then carry on with PORT-PLAN §5.
Date: 2026-10-10, evening. Everything below is measured on this Deck, in this round.

---

## P3 — `LONG` and `ULONG` were 8 bytes. Now they are 4.

`/home/deck/lena/.lena_cmr2/port/platform/platform_types.h`, lines 29–30, was:

```c
typedef long          LONG;    // 8 bytes on x86_64 LP64 -- not Win32
typedef unsigned long ULONG;
```

The comment above it said "Win32 LP64->our LP64", which is the category error:
Win32 is **LLP64**, and the build this decomp came from is 32-bit anyway. Win32
`LONG` is 4 bytes on every platform it has ever run on. On x86_64-linux `long`
is 8. It compiled fine and silently doubled every `POINT` / `RECT` / `SIZE` /
`MSG` and everything holding one.

Now:

```c
typedef int32_t  LONG;    /* Win32 LONG is 4 bytes, always */
typedef uint32_t ULONG;   /* and so is ULONG */
```

**Measured, `work/P3/p3_probe.cpp`, before → after, with the i386 build of the
SAME probe as the reference row** (the original is a PE32, so i386 is the truth):

| type | x86_64 PRE | x86_64 POST | i386 (reference) |
|---|---|---|---|
| `LONG` / `ULONG` | 8 / 8 | **4 / 4** | 4 / 4 |
| `POINT` | 16 | **8** | 8 |
| `RECT` | 32 | **16** | 16 |
| `SIZE` | 16 | **8** | 8 |
| `MSG` | 48 | **40** | 28 |
| `(LONG)(INT32_MAX+1)==INT32_MIN` | 0 (no wrap) | **1 (wraps)** | 1 |

`MSG` is 40 and not 28 because `hwnd` is a *real* 8-byte pointer here — that
difference is the shim being a 64-bit program, and Win64's own `MSG` is 48.
Every non-pointer field now matches the reference exactly.

**Same class of bug, measured, deliberately NOT changed in this step** (so it is
on the record rather than in my head):

| type | x86_64 | i386 |
|---|---|---|
| `HRESULT` | 8 | 4 |
| `LPARAM` / `LRESULT` | 8 / 8 | 4 / 4 |

`LPARAM`/`LRESULT` are left pointer-capable on purpose (that is the Win64 model,
and window messages carry pointers in `lParam`). `HRESULT` is a plain 32-bit
status code on every Win32 and is the one candidate left; no symptom has been
seen from it, so it is a note, not a change.

### P3's two side effects, both measured

1. **It unlocks nothing by itself.** Sweep on the unmodified tree = **27 PASS /
   39 FAIL**; sweep after P3 only = **27 PASS / 39 FAIL**, same file list, same
   first-error classes (`work/P3/sweep-pre` vs `work/P3/sweep-post`, `diff` of the
   two result files is empty). It is a correctness fix, not a count fix.
2. **It is real, and it is an ABI change.** 10 of the 27 objects changed bytes
   (compile is deterministic here — see below), and **14 mangled symbols change**,
   because `LONG` appears in signatures:

   * `Input.cpp`: 12 — `CreateDamperEffectEjllii` → `Ejiiii`,
     `CreateSpringEffectEjllii` → `Ejiiii`, `SetConditionCoefficientEili` →
     `Eiii`, `CreateConstantForceEffectEjlljjjjii` → `Ejiijjjjii`,
     `CreateForceFeedbackEffectEijllii` → `Eijiiii`,
     `SetEffectGainAndDirectionEijli` → `Eijii`.
   * `rhi/deck_dd7.cpp`: 2 — `DeckSurface7::GetOverlayPositionEPlS0_` → `EPiS0_`,
     `SetOverlayPositionEll` → `Eii`.

   **Consequence, and it is the reason to do P3 now rather than later: every
   prebuilt object is invalidated by this typedef.** A partial rebuild links
   against the old names or against objects that saw the old header. Doing it while
   the tree is small is cheap; doing it after more prebuilt objects accumulate is
   not.

Determinism check, so the "10 objects changed" claim is not path noise:
compiling `Font.cpp` twice into the same directory gives byte-identical objects
(`md5 b4d9631261fbdb6b97491e1804a6e888` both times), so an object difference
between the two sweeps is a real code difference.

---

## P1 — the 7 pointer-bearing size asserts: re-expressed, not deleted

The decomp asserts its own 32-bit layouts:

```c
typedef char StageNodeTablesSize[sizeof(StageNodeTables) == 0x14 ? 1 : -1];
```

17 of these exist. **10 hold no pointers and still pass unchanged on x86_64** —
they are a free correctness oracle and were left EXACTLY as they were. Verified two
ways this round: clang's layout dump gives them their original sizes with no
pointers in them (`ReplayLevelState` 0x64, `FireworkRocket` 0x938 ...
`TimerPulseState` 0x608), and **none of the ten names appears even once in the
post-P1 error census** — `grep -c` over `work/P3/sweep-p1/all_errors.txt` is 0 for
all ten, including the three whose TUs were never dumped
(`PlayerOptionCacheSize`, `RallyPairingTablesSize`, `ChampionshipTablesSize`).

The 7 that hold pointers were re-expressed so they keep BOTH oracles:

```c
typedef char StageNodeTablesSize[sizeof(StageNodeTables) ==
        (sizeof(void*) == 8 ? 0x28 : 0x14) ? 1 : -1];
```

**The 64-bit numbers are not hand-computed — they are clang's own record layout
dump** (`-Xclang -fdump-record-layouts`, over a throwaway copy of the tree at
`work/P3/tree-measure` with the asserts neutralised):

| struct | 32-bit | 64-bit | delta | why |
|---|---|---|---|---|
| `StageNodeTables` | 0x14 | **0x28** | +20 | `void *nodes[4]` |
| `StageArchiveTables` | 0x188 | **0x308** | +384 | nested `archives[32]`, 3 pointers each |
| `InputDeviceState` | 0x2864 | **0x2868** | +4 | one `LPDIRECTINPUTDEVICEA` |
| `InputFeedbackState` | 0x1c0 | **0x360** | +416 | `ForceFeedbackDevice[8]` |
| `CollisionBox` | 0x98 | **0xa0** | +8 | `int *pArray, *pVertex` (Collision2D.cpp) |
| `PartState` | 0x1a0 | **0x1b8** | +24 | 4 pointer members |
| `StageObjectDraw` | 0xa0 | **0xb0** | +16 | 2 pointer members (Game.cpp) |

Patch: `work/P3/patch_p1.py` (idempotent, backs up each file as
`*.bak-pre-p1-<stamp>`).

### Result

| sweep (x86_64-linux-gnu, 66 TUs, real `-c`, include order PLAT,ROOT,SHIM) | PASS | FAIL |
|---|---|---|
| control, unmodified tree | 27 | 39 |
| after P3 | 27 | 39 |
| **after P3 + P1** | **33** | **33** |

Newly compiling: `CarPhysics.cpp`, `FixedPoint.cpp`, `FrontendNetworkRally.cpp`,
`main.cpp`, `Stage.cpp`, `TrackCollision.cpp`. **Regressions: none** (the PASS set
only grew — `diff` of the two sorted PASS lists).

**And now there is exactly ONE failure class left in the whole tree:**

```
33 FAIL  --  all 33 are:  cast from pointer to smaller type 'int' loses information
```

1,504 error lines, **1,377 distinct sites**, 33 files. Ranked:
`FrontendMenus.cpp` 305, `FrontendScreens.cpp` 236, `GameInfo.cpp` 176,
`StageObjects.cpp` 135, `StageTiming.cpp` 108, `RallyData.cpp` 94, `Car.cpp` 70,
`GameMenus.cpp` 60, `Game.cpp` 32, then 24 smaller files.

Note what the count change means: the size asserts were the *first* error in 24
files, so fixing them revealed that 18 of those files have pointer casts waiting
behind. That is the honest headline — P1 removed a blocker, it did not remove the
work. The remaining wall is **P2 and only P2**.

---

## Verification — does any of this break the port that actually runs?

The x86_64 census is the mandate; the binary that boots today is the **i386**
build (`link32.sh`, `x86-linux-gnu.2.43`). So the regression test had to be on
i386, end to end, and it was:

1. **Full rebuild of all 66 game objects** against the P3+P1 headers, into a
   private object dir (`work/P3/i386/obj`, so the running worker's objects were
   not touched): **PASS 66, FAIL 0**.
2. **`deck_dd7.o` rebuilt too** — it includes `platform_types.h` and two of its
   mangled names change (above). Building it was not optional.
3. **Platform layer + link:** 5/5 platform objects, **link exit 0**, zero
   undefined symbols. `work/P3/i386/link-p3.sh`.
4. **Ran it for 20 seconds in a real window** from the patched build, and ran the
   **pre-change binary** the same way as a control. Normalised logs (addresses and
   timings stripped): **59 lines each, `diff` empty — byte-identical behaviour.**
   Both reach the same place: pad found, RHI up at 1280x800 1:1 fullscreen,
   `[DD7] CreateDevice -> RHI up`, and the same three message boxes
   ("Setting configuration to defaults" + 2× "Make sure the CMR2 CD is in the CD
   drive") — which are a **pre-existing** property of the game root's missing
   config, not of this change. GAME2-res is already on that; this run neither
   caused it nor hid it.

Logs: `/tmp/cmr2run-p3.log` (patched) and `/tmp/cmr2run-control.log` (control).

---

## What I did NOT do, so nobody assumes it

* `HRESULT` / `LPARAM` / `LRESULT` are still 8 bytes (measured, on purpose).
* **P2 is untouched.** Not one cast site has been changed. 33 files still fail.
* The published repo's stale numbers are only fixed if the commit below says so.
* Nothing was installed, no `sudo`, nothing downloaded, no protected data read.

## Next step (P2, and it is one step)

`(int)` → `(intptr_t)` on the 1,377 sites, **plus widening the fields that store
the result** — a mechanical sed that only changes the cast is a bug factory,
because the value often lands back in an `int` field that has to grow too. Start
with the two files that are 36% of the total (`FrontendMenus.cpp`,
`FrontendScreens.cpp`), measure, then continue. ASAN still does not build, so
every number here stays compile-level.
