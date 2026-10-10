# FRAME-FEEDBACK — Miami's three facts, chased to numbers

Written 2026-10-10 ~15:20 EEST by the worker on the feedback addendum.
Build measured: `src/port/cmr2deck.c` @ 15:15:00, binary built 15:15:02.
Every claim below is a command output or it is labelled as my reading.

---

## 0. VERDICT IN ONE LINE

His three facts are all real, and all three describe **this morning's build** —
not the current one. Both bugs are already gone. Nothing in his report needs a
new fix; one item (the wheels) needed chasing anyway, and it now has a number
instead of an opinion.

I was wrong twice on the way there. §3 says where.

---

## 1. WHICH FRAME HE SAW — evidence, not guess

He reported at **14:55**. The addendum asked me to check what he *could* have
looked at before assuming it was feedback on the current build. So:

* `screenshots/` **did not exist** before commit `66d32d1` at **15:08:43**.
  The last state he could have read was `e805a8d` (14:39), and
  `git ls-tree -r e805a8d` contains **zero image files** — LICENSE, README,
  docs, src, tools and nothing else.
* Every legible car frame on this machine is stamped **15:08 or later**
  (`/tmp/cars/*.png` 15:10+, `/tmp/liv_*.ppm.bmp` 15:13, `screenshots/*` 15:12).
* The only pre-14:55 car frames anywhere are
  `cmr2deck/shots/205-textured-yaw{38,128,218,308}.png` @ **09:45** — and they
  are **96–97% pure black** (mean RGB 3–4; only 1.1–2.6% of pixels above
  luminance 40, scattered as noise; `tools/bbox.py` on them prints a
  full-width bbox with no coherent shape). Nobody reads "the wheels are pretty
  on par" off those.
* The phone's own sandbox frames were **untextured** (his own caveat) — they
  cannot be "correct textures on the wheels".

**Conclusion: he looked at the LIVE WINDOW of the morning build, not a file.**
So the caveat resolves the *other* way from how it was offered: it is not
"probably the current frames" — it is certainly **not** the 15:08 frames, which
did not exist. His words are evidence about a build that is now superseded.
That makes them precious and it makes them non-actionable at the same time.

Two consequences worth saying out loud:

* The 09:45 PNG captures are **broken** (96% black). Whether the *renderer* was
  black or only the *save path* was is not something I can settle from here —
  the same `--shot` mechanism produces good frames now. That is a real and
  unfixed unknown, and it is the difference between "broken capture" and
  "broken frame". It deserves a look on its own account, not his.

---

## 2. HIS FACTS 1 AND 2, REPRODUCED EXACTLY

The morning build bound textures by **matching part names** against the texture
table. The current build still carries that path behind `NAMETEX=1`, for exactly
this A/B. Same car, same camera:

    OLD  (NAMETEX=1)   0:AP5NWBDf x505  3:AP5BodBu x8  4:AP5NWhDf x288
                       13:ap5glidf x56  22:ap5unddf x13  24:ap5ligbr x18
    NEW  (file's own)  27 runs, 8 distinct textures, 0 refused
                       0:AP5NWBDf x48  2:ap5dbodf x496  4:AP5NWhDf x144 ...

Read the OLD line against his two sentences:

* wheels → **`AP5NWhDf`**, the correct wheel texture → *"the correct textures on
  the wheels"*, *"pretty on par"*.
* **505 triangles** → `AP5NWBDf`, the near-black body texture (mean RGB
  17,17,19) → *"the bodywork textures were being applied with wrong UVs, so the
  body rendered grey/dark/blackish instead of its real colour"*.

That is not a resemblance, that is a match. Whole-frame statistics, same camera:

| frame | mean RGB | saturated pixels | max luminance |
|---|---|---|---|
| OLD `NAMETEX=1` | (23, 25, 28) | **0.0%** | 201 |
| NEW file ids | (45, 46, 49) | **4%** | 255 |

Zero saturated pixels in the old frame: the car is grey, end to end. His
description of the body is a measurement.

One correction to his wording, and it matters: **it is not the UVs.** The
coordinate mapping was fine; the *bound texture per part* was the wrong one.
UVs and bindings are different bugs, and chasing UVs would have been a wasted
day. The game's own rule for which texture a triangle wears is the int32 at
`MeshTriangle + 4 + field_0x2c * 4`, and the current build reads it.

---

## 3. THE WHEELS — I WAS WRONG TWICE, THEN MEASURED

His phone pointed at the c18 node block. That was the right place to look, and
`tools/` already had `patch_m2_nodes.py` for it.

**Wrong #1.** My first hypothesis was that "sunk in" was *only* a perceptual
artefact: with the body a near-black mass, the arches and sills vanish, so the
wheels look embedded in a blob. That is a real effect and it is visible in the
A/B above — but it is **not the whole story**, and I would have filed a real
geometry bug as a cosmetic one.

**Wrong #2.** I tried to re-derive per-part geometry from the file by hand and
got a unit cube out of the first offset I guessed, then a bbox 0.13 m short of
the binary's on the second. The binary's own numbers are authoritative; mine
were archaeology. I stopped guessing bases and used its output.

**The measurement.** The morning build placed wheels from hand-guessed
constants — `wx=1.20, wy=-0.23, wz=0.75`. `NODEPLACE=0` still runs that path:

    OLD  (NODEPLACE=0)  bbox x[-1.909,1.908] y[-0.557,0.723] z[-0.888,0.888]
                        = 3.82 x 1.28 x 1.78 m
    NEW  (file's nodes) bbox x[-1.909,1.908] y[-0.685,0.723] z[-0.879,0.879]
                        = 3.82 x 1.41 x 1.76 m

Read the y range, because that is the whole thing:

* **OLD:** the car's lowest point was the **body** (−0.557), not the tyres.
  Ground clearance **0.000 m**. The tyres sat entirely inside the body's
  vertical envelope — buried in the arches, nothing hanging below the sills.
  *"Sitting too far inside the bodywork — sunk in, too low/deep in the arches"*
  is that, as a number.
* **NEW:** wheels at the file's own scene-node transforms —
  `(1.239, −0.358, 0.717)`, `(1.239, −0.358, −0.718)`, `(−1.300, −0.358, 0.718)`,
  `(−1.300, −0.358, −0.718)` — giving wheelbase **2.539 m**, track **1.435 m**,
  and the tyres now reach **−0.685: 0.128 m of real ground clearance** below the
  bodywork.

**Two independent cross-checks, so this is not one number agreeing with itself:**

1. **The mesh has no hidden offset.** The wheel part is 0.654 × 0.654 × 0.276 m
   and its local bbox centre is exactly **(0.000, 0.000, 0.000)** — the node
   translation *is* the placement. There is no mesh-space bias to blame, and no
   double-counting to look for.
2. **The transform extraction is right.** `NODEDEBUG=1` on node 1 prints
   `65536 0 0 0 / 0 65536 0 0 / 0 0 65536 0 / 81179 -23482 46961 65536`. That is
   right/up/forward/position as four 4-word rows, row-major, translation in the
   **last row** — which is what the reader takes. No sign flip, no missing
   offset, no stray scale. The wheels' `±z` and the two 180°-about-up mirrored
   nodes come out symmetric.

Flushness, for completeness: wheel outer face |z| = **0.855** against the body's
widest **0.879** → recessed **0.024 m**. That is flush, not sunk. And the two
posts differ, correctly: 1.239/1.300 m front/rear.

---

## 4. WHAT I AM NOT CLAIMING

* **I never saw the morning window.** I am reading the build that was on disk at
  09:45 and re-running it. If he looked at something that is not on this
  machine, §2 and §3 are void and his eyes outrank mine. But two independent
  numbers matching two independent sentences is hard to get by accident.
* **I cannot tell whether the 09:45 capture broke or the 09:45 render broke.**
  See §1.
* **This is still the viewer, not the port.** The port cannot draw a car; the
  state tracker for 277 of 298 D3D7 call sites does not exist. Everything in
  §2/§3 is about the viewer drawing the game's own data. `docs/FRAME-NOTE.md`
  already says this and it stays true.

---

## 5. WHAT ACTUALLY NEEDS DOING

1. **Nothing from his report.** Do not re-open the body texture path — it is
   read from the file now. Do not "fix" the wheel transform — it is the file's
   own and it measures correct. Both would be regressions dressed as fixes.
2. **The 09:45 capture question.** A `--shot` run that yields a 96%-black PNG
   while the window is fine will bite whoever next tries to show him a frame.
3. **Unchanged and still the real gap:** the D3D7 state tracker.

---

## 6. EVIDENCE IN THIS REPO

| file | what it is |
|---|---|
| `docs/feedback/miami-saw-OLD-namematch-1280x800.png` | the morning build re-run (`NAMETEX=1`), same camera. Wheels textured, body a grey/black mass. **This is my reproduction of the failure he described, not a frame he saw.** |
| `docs/feedback/current-fileTextureIds-1280x800.png` | the current build, identical camera. Same car, textures read from the file. |

Both rendered on this Deck by `src/port/cmr2deck.c` @ 15:15:02. The camera is
fixed (`YAW=38 ELEV=12 DIST=0.95`) so the two are comparable pixel for pixel.
