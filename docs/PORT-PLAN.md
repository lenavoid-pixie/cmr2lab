# The native port — plan, measurements, and what is still unverified

**Target: Colin McRae Rally 2.0 (PC, 2000) compiling and running as a native
x86_64 Linux binary. No Wine, no Proton, no compatibility layer.**

This document is the *measurement* companion to `PORT.md`, which is the summary.
Everything numeric here was produced by a command that is either in this repo or
written out in full, and nothing here should be quoted without its method. Where
a number could not be reproduced it is marked **withdrawn** rather than deleted,
so the next reader does not re-derive it by accident.

The decompiled source this work measures is a separate, public, GPL-licensed
matching decompilation of the PC build (`pablocpas/CMR2Decomp`, itself building
on the earlier `CMR2Decomp/CMR2Decomp`). **This repository ships no part of it**
— only the tools used to measure it and the numbers that came out.

---

## 1. The tree, measured

```
 66  .cpp      127,081 lines
 56  .h          8,724 lines
122  files     135,805 lines
```

*(Provenance note, added 2026-10-10 evening: this census is the tree **as received**.
Our own port edits since — the `LONG` typedef and the 7 re-expressed size asserts — add
235 lines, so the same command reads 122 files / **136,040** lines today. The file count
is unchanged; only our own work moved the line count. Nothing else here needs re-reading
because of it.)*

Every function carries a `// FUNCTION: CMR2 0xADDRESS` marker: **3,364** of them.

| classification | count |
|---|---|
| `REAL` — multi-statement body, does work | 2,476 |
| `ACCESSOR` — single state-touching return, i.e. real decompilation | 868 |
| `ASM` | 2 |
| **genuine `PLACEHOLDER`** | **16** |
| unparsed | 2 |

**3,344 of 3,364 (99.4%) have a decompiled body. This is a real decomp, not a
stub tree.** An earlier pass of mine reported "463 stubs"; that classifier called
any one-line body a stub, and in this tree a one-liner is usually the actual
recovered implementation:

```c
// FUNCTION: CMR2 0x004057f0
unsigned char CGameInfo::GetGameLanguage(void)
{ return m_gameInfo.field_0x14 & 7; }
```

Sixteen placeholders is a rounding error and is not the reason the port is hard.

---

## 2. The Windows surface, re-measured

Method: regex `\bpD3D\s*->\s*NAME\s*\(` over `*.cpp *.h` of the decomp tree.
Reporting the pattern matters, because two earlier passes published numbers that
a different pattern produced and did not say so.

### 2.1 Direct3D 7

**278 call sites, 20 distinct methods.** 225 of them (81%) are in `Graphics.cpp`.

```
113  SetTextureStageState      6  GetTransform        3  Clear
 75  SetRenderState            6  SetLight            3  BeginScene
 19  LightEnable               3  SetViewport         2  SetTexture
 18  SetTransform              3  EndScene            1  SetRenderTarget / SetMaterial
 10  DrawIndexedPrimitiveVB    3  DrawPrimitiveVB     1  Release / GetCaps
  8  DrawPrimitive             1  DrawIndexedPrimitive   / EnumTextureFormats
```

**22 are draws. 256 are state.** That ratio is the whole architectural problem:
SDL3_GPU has no `SetRenderState` — it binds immutable pipeline objects — so a
1:1 mapping dies at the first state call. The port needs a **state tracker and a
pipeline cache**.

Also measured: `pDD->` (device creation / DirectDraw) is a separate surface of
**20 call sites across 7 methods**, and the game is D3D7 *fixed-function with no
shaders at all* — it submits `D3DTLVERTEX`, transformed-and-lit screen-space
triangles. There is no transform and no lighting math to reimplement; the game
already did both in software.

### 2.2 Win32 and assembly

| | |
|---|---|
| files that `#include <windows.h>` | **45** |
| `__asm` occurrences | **36** (35 excluding one in a comment), in **6** files |
| `__asm` blocks visible to a non-MSVC compiler | **0** |
| distinct structs whose `sizeof` assert breaks on 64-bit | **7** |

Every `__asm` block sits inside `#ifdef _MSC_VER` with a portable `#else`. This
was the question most likely to kill the port and the answer is zero. The one
real caveat: the `#else` for float→16.16 uses a truncating cast where the asm
used `fistp` (round-to-nearest-even), so those 5 sites can differ by 1 LSB on
negative inputs. Determinism, not compilation — fix with `nearbyintf` later.

### 2.3 The three numbers that were wrong

The first pass published "149 D3D7 calls / 14 Win32 calls / 5 files containing
asm" in `PORT.md`. All three were wrong, all three have been replaced:

| published | re-measured | cause |
|---|---|---|
| 149 D3D7 calls | **278** | pattern matched bare `D3D*` identifiers, not `pD3D->Method` |
| 14 Win32 calls | **45 files** `#include <windows.h>` | include census ran without `re.M` |
| 5 files with `__asm` | **6 files, 35 blocks** | counted per-file matches, missed `FixedPoint.h` |

A fourth: "225 call sites / 16 methods" is **correct but scoped to
`Graphics.cpp` alone** — it is the number from `RHI-MEASUREMENT.md`, which says
so, and it should be quoted that way. Tree-wide it is 278 / 20.

---

## 3. The build — two targets, and what the gap actually means

Harness: `tools/sweep-compile.sh`, one translation unit at a time, `-c` into a
real object, `-ferror-limit=0` so nothing is truncated, include order
`platform, root, shim` (load-bearing; shim-first compiles 0/66).

```
                                        pass   fail
x86_64-linux-gnu  (the mandate)           26     40
x86-linux-gnu     (i386, kept as oracle)  53     13
```

*(This block is the pre-P3 measurement, kept as the record — see §6 for what
replaced it. **Current, re-run on the Deck on 2026-10-10 evening: 33/66 on
x86_64, and 66/66 on i386 — the second only with `-Wno-c++11-narrowing`, the flag
the i386 build actually uses.** Without that flag the same sweep reports 64/66:
`Game.cpp` and `GameInfo.cpp` put values above `INT_MAX` in 32-bit `case` labels.
`tools/sweep-compile.sh` now passes it and reproduces both counts.)*

This pair is a diagnostic, not two options:

* **27 files fail on x86_64 and pass on i386** — those 27 are blocked by nothing
  but pointer width.
* **0 files pass on x86_64 and fail on i386.**
* **13 files fail on both targets** — and they fail on *names*, not width:
  12 of them on a single missing typedef, `LPHWAVEOUT`, and one (`Input.cpp`) on
  an undeclared `IID_IDirectInput7A`.

### 3.1 I tested the "one typedef fixes 13 files" idea instead of asserting it

Adding `typedef void* LPHWAVEOUT;` to the include line and recompiling exactly
those 13 files:

| added | result |
|---|---|
| `LPHWAVEOUT` | **0 of 13 pass** — moves to `LPHWAVEIN` (5 files) and the size asserts (7) |
| `+ LPHWAVEIN` | **0 of 13** — moves to `IDirectSoundBuffer` (2) and the casts/asserts (9) |
| `+ MMRESULT` | **0 of 13** — no change |

**The shim gap is a queue, not a wall.** Every name you add reveals the next
one, and behind them is the pointer-width work that was never going to be
avoided. This is the single most useful thing I measured this round: the
tempting story ("13 files just need a typedef") is **false**, and the real order
of work is the one in §4.

### 3.2 Error census, `x86_64` only

**1,751 errors**, classified from the compiler's own text:

| class | count |
|---|---|
| `cast from pointer to smaller type 'int'` | 1,300 |
| `use of undeclared identifier` | 181 |
| `unknown type name` | 120 (14 distinct names) |
| incomplete COM type | 65 |
| `declared as an array with a negative size` | 54 |
| everything else | 31 |

The 54 "negative array size" errors are **7 distinct structs**, and they are the
decomp's own byte-exact layout asserts — a free correctness oracle that the
original 32-bit binary satisfied and x86_64 cannot:

```
InputFeedbackStateSize   17 errors     InputDeviceStateSize  17
StageNodeTablesSize      12            PartStateSize          4
StageArchiveSizeCheck     2            StageObjectDraw_size   1
CollisionBoxSize          1
```

Five of those structs are declared in headers, which is why one broken assert
cascades into a dozen files. Note the asymmetry: the count above is *compiler*
evidence. A static scan of the same tree reported `ptr_members=0` for
`InputDeviceState`, which the compiler flatly contradicts — **when the compiler
and the scanner disagree about a type, the compiler is right**, and the scanner
is only good for ranking, not for a headline number.

### 3.3 A latent bug worth fixing first

`platform/platform_types.h`:

```c
typedef long          LONG;    /* 8 bytes on x86_64 LP64 -- WRONG */
typedef unsigned long ULONG;   /* 8 bytes -- WRONG */
```

**Win32 `LONG` is 4 bytes on every platform that has ever existed.** The header's
comment says "Win32 LP64 → our LP64", which is a category error — Win32 is
**LLP64**. This compiles cleanly today and silently changes `POINT`, `RECT`,
`SIZE` and every struct holding a `LONG`. One line: `int32_t` / `uint32_t`.

---

## 4. The BFL container — why most cars still draw untextured

`PORT.md` used to say "textures 0/27" and blame a truncated final TOC record.
That is real but it is not the main reason. Measured across the **220 `.bfl`
containers in `Game/Cars`**:

```
176  hold .tga  (raw, uncompressed truecolour)
 44  hold .dds  (DXT-compressed)
```

and the viewer's `bfl_parse` accepts only the second kind — not because of the
extension test, which accepts both, but because of how it **scores** a candidate
table of contents:

```c
if (!memcmp(p + off, "DDS ", 4)) val++;
else if (!memcmp(p + off + 8, "DDS ", 4)) val++;
else if (!memcmp(p + off, "-XFILE.", 7)) val++;
```

A `.tga` container validates zero of its records, so `bestscore < 1` and the
whole container is rejected. Verified against the bytes: in `205a1.bfl` the first
texture block begins `DDS `, and in `205c1.bfl` it begins `00 00 02 00 …` — an
uncompressed TGA header. **Why the DXT bit works today: the reader is really
validating DDS blocks and calling the result "textures".**

Two independent readers in this repo already parse both kinds correctly
(`tools/bfl-read.py` reads all 220 containers, including the 176 TGA ones), which
is how the gap was isolated: same files, same structure, one reader scores them.

---

## 5. Order of work

1. **P3** — `LONG`/`ULONG` → `int32_t`/`uint32_t`. One line, helps both targets.
2. **BFL** — score `.tga` blocks too, and add a TGA decode path. Turns ~80% of
   the fleet from flat white into textured. Small and self-contained.
3. **S1 + S3** — SDL3 window/message pump + file/time behind the Win32 names the
   decomp already calls. `main.cpp` links; nothing else needs changing.
4. **P1 + P2** — the 64-bit pass: relax the 7 pointer-bearing size asserts (keep
   the other 10), `(int)` → `(intptr_t)` at ~1,300 sites. Mechanical and large;
   *the* cost of the mandate. **27 files come back for free** on the day this is
   done — they pass on i386 already.
5. **S2 — the RHI backend.** The mountain: 256 state calls, 22 draws, no
   `SetRenderState` in SDL3_GPU. Start from the states the *first frame* actually
   sets (trace them), not from the full enum.
6. **First frame** — `WinMain` → `CMain::Initialize` → `Game_DrawSceneViewport`
   (`0x0049d3f0`), real CMR2 code, one frame, windowed.

## 6. Not verified, and should not be claimed

* **Only 6 of 259 cars have been run through the viewer this round.** All six
  exit 0, but four give a car-length bbox (3.81–3.82 m) and two — `205a5`,
  `205c5` — give 1.76 m. Those two load 992 / 756 vertices of parts named
  `20interiorA`, `27swheel`, `26gearstic`, `41Speedo`: a cockpit, not a car
  body. **Hypothesis** (untested): those `.c3d` files are interior meshes, and
  the bbox is correct for what they contain. Until that is tested, "the loader
  works on the fleet" is not a claim anyone should make.
* **No sanitizer exists for this tree.** Every correctness statement here is
  empirical — no abort, reconciled capacity, byte-exact payload arithmetic. That
  is weaker than instrumented evidence and it is how a 12-byte vertex-base error
  survived a plausible-looking render.
* **The i386 sweep is a diagnostic only.** It is not a shipping target and
  building for it would have to be redone.

## 7. Ledger — numbers I published and then had to fix

Kept because the pattern matters more than the individual errors: every one of
these was reported before it had been run.

| said | true | how it was caught |
|---|---|---|
| 463 stubs | 16 placeholders | classifier called accessors stubs |
| 19/66 compiles | 26/66 x86_64, 53/66 i386 | it had never been run |
| 26/66 x86_64, 53/66 i386 | **33/66 x86_64, 66/66 i386** after P3+P1 — i386 only with `-Wno-c++11-narrowing` (64/66 without; see §3) | re-measured 2026-10-10 18:20, `docs/P3-P1-REPORT.md`, re-run 18:5x |
| 149 D3D7 calls | 278 call sites / 20 methods | wrong regex, stated anyway |
| 14 Win32 calls | 45 files include `windows.h` | regex ran without `re.M` |
| 5 files with `__asm` | 6 files, 35 blocks | per-file count, missed a header |
| textures 0/27, TOC | 44 of 220 containers are DDS and parse; 176 are TGA and are rejected | read the scorer, not the symptom |
| bridging 0.00% vs 0.68% | 0.00% for **both** readings | re-ran it; **withdrawn** |
| "strip invents 1,176" | strip *yields* 1,176 — 288 more than the file's 888 | arithmetic, then the viewer printed `132.4%` |

---

## 8. The dependency map — added after this plan was written

Every number above is a **surface census**: how many sites mention a symbol. None
of them is a **dependency graph**: who calls what, in what order, and what stops
if a piece is absent. The shim was therefore being built in *compiler-error
order* — the order the compiler happened to complain in.

Two artifacts fix that, both in this repo:

* `DEPENDENCY-MAP.md` + `tables/` — the static call graph. Every Win32 symbol
  with the translation units that call it and the site count
  (`tables/win32_by_tu.csv`), the 14 class-A type names resolved to the structs
  that declare them (`tables/classA_types.csv`), and the WinMain→first-frame
  chain (`tables/critical_path.csv`). Tools: `tools/depmap.py`,
  `tools/depmap-report.py`.
* `RUNTIME-RELAY.md` — the same layer **measured instead of counted**. Wine is
  the reference implementation of the layer being replaced and it is already on
  the machine, so `CMR2.exe` under `WINEDEBUG=+relay` prints every Win32 call in
  order on the real code path. Tools: `tools/relay-run2.sh`,
  `tools/relay-scan.py`.

The trick that makes the runtime half readable: relay prints a return address
for every call, and `functions.tsv` carries address *and size* for all 3,650
functions, so each call resolves to a decompiled function by exact containment.
The trace becomes *"which decompiled function fires which Win32 call, in order."*

### Numbers this adds, with their scope stated

| quantity | value | scope |
|---|---|---|
| distinct Win32 symbols used | **85** | tree-wide, full Win32 name set |
| Win32 call sites | **221** | comment/string-blanked scan |
| D3D7/COM method sites | **278 / 298** | `pD3D->` only / `pD3D->`+`pDD->` |
| — of those, draws / state | **22 / 256** and **22 / 276** | both scopes, measured 2026-10-10 |
| Win32 symbols the census missed but that fire on the boot path | **15** | relay vs grep |
| decompiled functions that fire before a first frame | **23** | one 50 s run |

**UPDATE 2026-10-10 (worker `seq-s2-first`, reproducible: `tools/d3d_census.py`,
one command, no arguments).** 278 and 298 are confirmed exactly, and they are the
same census at two scopes: `pD3D->` = 278 sites / 20 methods / 22 draws / 256
state; adding `pDD->` = 298 / 26 / 22 / 276. **518 is NOT reproducible** and should
not be re-quoted without its definition: counting every `x->Method(` in the tree
gives **533**, and a D3D7-method-whitelisted count gives **400**. The count is not
comment text -- blanking `//` and `/* */` gives the same 278/298 -- but a regex
that stops at the first identifier gives **275**, because it loses every call
written `m_pTextureManager->pD3D->Method(...)`. That is the failure mode this
table exists to prevent.

The 278/298 row is not a correction of anything above — they are two
different scopes and both are defensible. Quoting one without its scope is the
error, which is the same one already recorded in §7.

### The runtime finding that matters most

Run with his prefix untouched, the shipped `CMR2.exe` performs **669 Win32
calls and exits after one second**, having never touched `ddraw`, `dsound`,
`dinput`, or a single asset file. `c:\error.txt` says *"Program finished
normally"* — because `CGame::InitializeGame` reads a SKU value from
`HKLM\SOFTWARE\Codemasters\Colin McRae Rally 2` and, if none of
Europe/America/Japan/Poland match, calls `CGame::SetShouldExit()`.

**A broken registry shim does not produce an error. It produces a one-second
silent exit with a "finished normally" log.** With the registry values supplied
(in a copy of the prefix, `WOW6432Node` — see `RUNTIME-RELAY.md` §2 for the
32-bit redirection trap) the game goes on to `CGraphics::InitializeDirectX`,
where `DirectDrawCreateEx` returns `DD_OK`, and then blocks on a modal
`MessageBoxA("Setting configuration to defaults")`. That dialog is the current
edge of what is measured; the D3D7 device and `Game_DrawSceneViewport` remain
unmeasured at runtime.
