# progress.md — Lena on the Deck, reporting to Lena on the phone

Rewritten **whole** at every checkpoint, never appended. If a line is not true at
the timestamp below, it is not in this file. Every line answers "how do I know
this" with a number, a path, or a test result — and where I could not answer it,
the line is in §6 instead.

**Last rewritten: 2026-10-10 14:28 EEST.** This file is one commit — the rewrite
that carried it is `git log -1 -- progress.md`, and it is rewritten whole, not
appended, so there is never a stale claim above a fresh one.

Paths, so they line up on both ends:

| short name | real path on the Deck |
|---|---|
| the install | `…/Steam/steamapps/compatdata/3646996028/pfx/drive_c/Program Files (x86)/Codemasters/Colin McRae Rally 2` |
| the port tree | `~/.lena_cmr2/port` (game + platform shims + `deckbuild/out/cmr2`) |
| this repo | `~/lena/work/cmr2lab-publish` |
| daemon state | `~/lena/state`, `~/lena/logs/daemon.log` |

---

## 1. RIGHT NOW

- **Daemon** `lena_daemon.py` pid **250660**, **2 workers**, model `deepseek-v4-flash`,
  uptime 4,760 s (~79 min), 400 steps per round.
- **Worker 1 = PROGRESS-REPORT** (this page), claimed 14:16:50, **step 55**.
- **Worker 2 = GAME2-r6-r2** ("make game work with Control of deck steam"),
  claimed 14:12:39, **step 63**, and this second it is running the port **under gdb**:
  `~/.lena_cmr2/port/deckbuild/out/cmr2` under `work/GAME2-r6/pad5.gdb`
  (pids 287846 / 287849 / 287866), reading the game's own controller records out of
  the live process — device `slot[2]`, the axis-binding table at `+0x470`,
  `m_controllerInfo[2]` at `0xfd3cec`. That is the last open question in its own
  checkpoint ("STILL TO MEASURE"), so a real runtime answer should exist shortly.
- **Queue:** 32 tasks — 17 `done`, 10 `partial`, **2 `running`** (those two),
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
confirmation is the run in flight in §1.*

**7. SDL3 gets a real GPU on this Deck.**
Device created with the `vulkan` driver on the real Radeon (RADV VANGOGH); a
triangle rendered offscreen and read back. I checked the artifact myself:
`state/probe_tri.bmp` is 128×128, 24 bpp, with **4,050 non-black pixels of 16,384** —
not a blank buffer. (It is 4,050 in both the Python and the C probe.)

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
  L2→axis 2 and R2→axis 5. The fix is port-layer (`axis1 = R2 − L2`), not written yet.

**B. The native port: 27 of 66 translation units compile** on the mandate target
(**41 %**), 1,561 errors left. The RHI state tracker — 277 of 298 D3D7 call sites
are *state*, and SDL3-GPU has no `SetRenderState` — is **not started**. No
measurable fraction: you cannot count code that does not exist.

**C. Dependency map (DEPMAP)**: static call graph + runtime relay trace + the
7-tier build order are on disk and documented (`docs/DEPENDENCY-MAP.md`,
`docs/RUNTIME-RELAY.md`, 5 CSVs), but the queue still says `partial`. That is queue
hygiene, not work — **no measurable fraction**.

**D. This report:** no fraction.

---

## 4. BLOCKED

**The one `blocked` entry in the queue is stale and should be closed.** `t2`
("can SDL3 render on this Deck's real GPU") is marked blocked; it was killed on
2026-10-09 because the probe popped modal SDL assert dialogs on Miami's desktop.
The question was answered afterwards, longhand, with a rendered triangle on disk
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
- **The queue's status field is not a status report.** It says `partial` for ten jobs
  that are finished and `blocked` for one that is not. This file is the status
  report; trust it over the queue.
- **Three commits were sitting on this Deck unpushed** (the dependency-map work).
  They go up with this file — so if you read the repo before 14:30 today, you did not
  see them.
