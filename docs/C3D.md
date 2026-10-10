# The `PP_F` geometry format

Shared by **cars and tracks**. Recovered from the loader at
`CMR2.exe + 0x004B93C0` — the function the `"PP_F"` string compare at
`+0x004B9389` calls when the magic matches.

## ★★★ TOPOLOGY — SOLVED: it is an INDEXED TRIANGLE LIST

Not a triangle strip. This was tested properly and settled by four independent
lines of evidence, and the first one is the only one that actually decides it.

**1. The game's own loader (ground truth).**
`Graphics.cpp:3209` builds an index buffer from `vertexIndex[0..2]` and calls
`DrawIndexedPrimitiveVB(D3DPT_TRIANGLELIST, ...)`. `Sector.cpp`'s
`Sector_RelocateStageMeshFile` (`0x004b93c0`) is the same reader this format
documents. That is the format's own source code stating its topology. It is not
an inference from a render.

**2. Statistical.**
Every offset and stride was brute-forced across all 259 cars, 2,075 parts and
119,753 face records. Offset `+0x40` yields a valid, non-degenerate index triple
in **99.968%** of records. The expected number of accidental hits over that many
records is about **0.0003**. Observed: 119,715.

**3. Count parity.**
Indexed yields exactly **888 triangles = the header's `c20`**, the recorded face
count. The strip reading yields **1,176** — that is **288 triangles the file does
not contain** (+32.4%) — and it would leave the whole 67 KB triangle block as
dead weight that the loader nonetheless walks.

`sum(part.F) == c20` in the **254 files whose header carries a `c20` at all**,
with **0 mismatches**; the other 5 of 259 files have no comparable count (`tools/
toposcan.c` prints `254 ok / 0 off`). The earlier phrasing here — "254/259" —
read as if 5 files disagreed with the reader. They do not; they have nothing to
compare.

Tree-wide the two readings are further apart than on this one car: indexed
**119,753** triangles against strip's `sum(V-2)` = **175,173** (+46.3%). The
per-car ratio varies because the strip reading costs `-2` per *part*, so cars cut
into many small parts drift furthest from the file's own count.

**4. Render, both ways.**
Four angles each, with `tools/compare_bmp.py`. Measured with **no `.bfl` beside
the car**, so every part draws flat white. That is the condition these numbers
are valid in, and it is not the viewer's default:

```
read      yaw   cover%   holes  comps
indexed    38   11.93%       0      1
indexed   128   11.75%       0      1
indexed   218   12.98%       0      1
indexed   308    9.37%       0      2
strip      38   11.94%     120      4
strip     128   11.83%     320      3
strip     218   13.15%     337      1
strip     308    9.37%     245      2
```

Indexed closes every enclosed background pixel and comes out as one piece
(two at the rear angle); strip leaves 120–337 enclosed background pixels and up
to 4 pieces.

**Caveat, because it bit once already.** Put the `.bfl` back and the same
comparison gives **1,455–2,797** holes for indexed and **2,935–13,849** for
strip. The ordering survives; the numbers do not. Transparent parts
(`07SemiTrans`) let the clear colour through, and an enclosed transparent window
reads as a hole. **A hole count is only comparable between builds that render
transparency the same way** — which is why the flat-white condition is stated
here instead of assumed.

### Why a render could never have settled this — stated plainly

Both readings produce a **recognisable car**, and the honest measurement of how
close they are is *worse* than this note first claimed. Measured on the current
viewer, flat-white condition, four angles:

| | of the model's pixels |
|---|---|
| outline differs | **1.1 – 2.4%** |
| shading differs | **37 – 50%** |
| enclosed missing patches | **0.1 – 0.3%** |

So the two readings agree on the *outline* and disagree almost everywhere else,
because the strip reading draws different triangles across the same surface. An
early strip render looked correct and was treated as confirmation; it was not
evidence. Finding the decompiled loader is what settled it. **A
plausible-looking silhouette is not proof, and neither is its absence.**

Two dead ends, recorded so nobody repeats them:

- **Edge-manifoldness does not discriminate.** A triangle strip is manifold *by
  construction* and scores **better** than the real mesh. Useless as a test.
- **The bridging metric does not discriminate either — and its published number
  is withdrawn.** This section used to claim 0.00% vs 0.68%. Re-run on the
  current viewer it prints **0.00% for both readings**: strip's longest-edge
  ratio (median, normalised by part bbox diagonal) *rises*, 0.1764 → 0.2035, and
  that is enough to pull every triangle back under the 5x-tail threshold. The
  0.68% is not reproducible from the code in this repo, so it is struck out
  rather than left standing as corroboration it cannot give.

### Status of these numbers — re-measured 2026-10-10

Everything still quoted above was re-run immediately before this was committed:
`toposcan` across all 259 cars (99.968%, 119,715 hits, `254 ok / 0 off`,
119,753 vs 175,173), `c3dprobe` on `205a1N` (888 indexed vs 1,176 strip), the
four-angle render pair, and the viewer's own output. Two things did **not**
reproduce and were corrected rather than left standing: the strip count was
being stated as *"invents 1,176"* when 1,176 is what strip **yields**, and the
0.68% bridging figure is withdrawn. The citation at the top of this section is
not a measurement — it is the game's own loader, and it is what decides this.

### The vertex base — an earlier version was 12 bytes late

The base was previously taken as `C14+12` with position at `+36`. That is
algebraically **the next vertex's position**, so every vertex was drawn carrying
its **neighbour's** coordinates, with normals and UVs read from the wrong fields.
It still produced a car-shaped silhouette, which is exactly why it survived so
long.

Corrected, the vertex block reconciles **byte-exact** against the payload, and
the result is a real **3.82 × 1.28 × 1.78 m Peugeot 205** — watertight, wheels
in the right places.

---

## Header — 48 bytes

```
+0x00  char  magic[4]   "PP_F"
+0x04  u16   ?          6 for a car body
+0x06  u16   ?
+0x08  u32   ?          (also present)
+0x0c  u32   rawOffset  a byte offset from the struct base
+0x10  u32   c10        stride 20
+0x14  u32   c14        stride 52
+0x18  u16   c18        stride 396
+0x1a  u16   c1a        stride 288   (grouped with c1c)
+0x1c  u16   c1c        stride 288
+0x1e  u16   c1e        stride 136
+0x20  u32   c20        stride 76    (10 relocatable pointers per record)
+0x24  u16   TEXCOUNT   number of textures named at the end
+0x26  u16   c26        stride 92
+0x28  u16   c28        stride 28
+0x2a  u16   ?
+0x2c  u32   ?
+0x30  ---   DATA BEGINS
```

## Data

A run of typed arrays starting at `+0x30`, in the order the loader walks them:

| count | stride | bytes |
|---|---|---|
| c18 | 396 | `c18 * 396` |
| c1a + c1c | 288 | `(c1a + c1c) * 288` |
| c1e | 136 | `c1e * 136` |
| c28 | 28 | `c28 * 28` |
| c20 | 76 | `c20 * 76` |
| c14 | 52 | `c14 * 52` |
| c10 | 20 | `c10 * 20` |
| c26 | 92 | `c26 * 92` |

## Trailer

Immediately after the arrays, two tables of `TEXCOUNT` records each:

```
name table   TEXCOUNT * 24 bytes    "Texture 0" .. "Texture N"
path table   TEXCOUNT * 260 bytes   the texture paths
```

The path records hold the original Codemasters build directories, still
present in the shipped retail files:

```
U:\Cars2\Escort\FEs\Hier\UsePC\FESGlODf
U:\Cars2\Escort\FEs\Hier\UsePC\fesglobu
...
```

Names in the `.c3d` are **mixed case**; the matching `.bfl` entries are
lowercase. **The lookup is case-insensitive.** Verified 6/6 on `escf3`.

## The identity

```
48 + sum(counts*strides) + TEXCOUNT*24 + TEXCOUNT*260  ==  file size
```

**259 / 259 car models close EXACTLY.** Verified, not assumed.
Stage road blobs close too — `temp.c3d` for `Sweden/Swe01Lo` closes exactly
at 2,894,356 bytes.

## Pointers

The `c20` records carry **10 u32 pointers each**. At load the engine walks
them and adds a runtime heap base held at `[0x65fa20]`:

```
mov  edx, [ecx]
cmp  edx, -1              ; 0xFFFFFFFF == null, skip
je   skip
mov  edi, [0x65fa20]      ; heap base
add  edx, edi
mov  [ecx], edx           ; write back
skip:
add  ecx, 4               ; 10 times per record
...
add  ecx, 0x4c            ; then advance 76 bytes
```

So the file stores **offsets**, and the engine turns them into **pointers** at
load. That is what makes the format position-independent and what would let a
converter lay out a new model at any address.

## What L and S and N mean

```
206a1N.c3d   163,248     body
206a1L.c3d    52,124     left wheel
206a1S.c3d    52,124     right wheel   (identical size — mirror)
```

`N` = body. `L` / `S` = the wheels.

## Still open

`temp.obj` and `temp.sht` — the scenery and billboard blobs inside a stage —
carry extra trailing data beyond the tables above (372,960 and 33,440 bytes
unaccounted). The base layout is correct for them; there is an additional
trailer to map. The **road** (`temp.c3d`) closes exactly, which is the one
that matters for a stage.

---

## Record semantics — status

The container, header and array layout are **solved and proven** (259/259 + a real
stage road). The *contents* of the records are partially decoded.

### What the records contain

`c20` (76 bytes) records hold **ten u32 pointers each**. The loader walks them and
adds a heap base at `[0x65fa20]` to every entry that isn't `0xFFFFFFFF`:

```
mov  edx, [ecx]
cmp  edx, -1
je   skip
add  edx, [0x65fa20]
mov  [ecx], edx
skip:
add  ecx, 4        ; ten per record
add  ecx, 0x4c     ; then stride 76
```

`c10` (20 bytes) records carry a **sequential index and a back reference**:

```
(3,1) (4,2) (5,3) (6,4) ...      u16[1] = u16[0] - 2
```

### The vertex data is quantized, not plain float

Scanning every byte offset in `205a1N.c3d` for runs of sane float32 returns
**zero** runs of 40 or more. The floats that *are* present are exact binary
fractions:

```
0.6015625        = 77/128
0.6405792236328125
0.648468017578125
0.113922119140625
```

Values like these come from **small integers scaled by a power of two** — the
classic signature of compressed vertex data. The mesh is in there. What is not
yet established is the **encoding and the field order**.

Recurring sentinels inside records:

```
0xFFFFFFFF            the null pointer the loader skips
-1.7014118346046923e+38    appears 93 times in c14 — a marker
denormals            27 times
```

### What that means for a port

Loading geometry is **not** blocked on anything structural any more — the file
parses, the arrays are located, the counts and strides are known, the trailer is
known. What remains is **decoding one record type**.

That is a bounded, findable problem, and the next step is to disassemble the
consumers of the `c14` and `c10` arrays — the functions that read them after
load — rather than staring at the bytes.

---

## Texture references — SOLVED

The mesh records do **not** name textures. They hold a **u16 index** into the
path table, and the engine resolves it at load.

The resolver is `0x4b9910`:

```
mov  ax, word [ecx]        ; read the u16 index from the record
shl  edx, 6                ; ×64
add  edx, eax              ; ×65
lea  ebp, [eax + edx*4]    ; -> table + index * 260
call 0x4a9f90              ; fold to UPPERCASE
push 0x5c                  ; '\'
repne scasb                ; find the last backslash
                           ; ... keep the filename
```

And `0x4a9f90` is the case folder — it walks the string and does
`sub al, 0x20` on anything in `a`..`z`, with a CP1252 special-character jump
table for `~ $ * | { }` and the Nordic range `0xE0`..`0xFF`.

**That is why `FESGlODf` in the `.c3d` resolves to `fesglodf` on disk.** The
mixed case is normalised to uppercase at load, and the lookup is
case-insensitive because the engine made it so.

### Verified against a real file

`escf3.c3d`, path table uppercased as the engine would:

```
[0] FESGLODF   [1] FESGLOBU   [2] FESBODDF
[3] FESBODBU   [4] DESLIGBR   [5] DESLIGRU
```

Scanning its three `c18` mesh records for u16 values below 6:

```
index 1 -> FESGLOBU   seen 45x   at rec0+90
index 3 -> FESBODBU   seen  1x   at rec0+376
```

**45 references to one material inside a single 396-byte mesh record.** The
indices are real, and they resolve.

### Also confirmed

`0x4b98f0` is a plain pointer fixup, called with `(ptr, base)`:

```
mov  eax, [ecx]
cmp  eax, -1
jne  .add
mov  dword [ecx], 0     ; -1  ->  0
ret
.add:
add  eax, edx           ; *ptr += base
mov  [ecx], eax
```

It is called as `(record, base+0)` and `(record, base+4)` for each `c18`
record — so a `c18` record begins with **two pointers that are fixed up
separately**.

---

## Vertex encoding — methods that FAILED (do not retry blind)

Recorded so the next attempt doesn't walk the same three miles.

**1. Dense float32 triples.** No run of 40+ sane floats at *any* byte offset in
`205a1N.c3d`. Vertices are not plain float triples.

**2. Dense int16 triples in car range (±6000).** Zero runs of 60+.

**3. Int8 triples as normals.** Found a 599-triple run of int8 at offset 138,255
with a tempting shift-by-one-byte neighbour — *looked* like packed normals.
**It is not.** Magnitudes: min 24.0, median 97.6, max 176.8 — only 310/599 land in
the 90–150 band a unit vector ×127 would give.

**4. "Packed stream" detection by run length.** This one is the trap. Scanning
every byte offset for long runs of non-zero int8 returns 317 candidates at
offsets 9 bytes apart, each exactly 3 triples shorter than the last. **That is a
sliding window over a high-entropy region, not a data structure.** The scan is
not detecting structure; it is detecting "most bytes here are non-zero."

### The missing control — the actual lesson

**None of the scans above had a negative control.** Shuffling the same bytes and
re-running the identical scan would have produced identical-looking runs, which
would have disqualified approaches 3 and 4 immediately, for free.

**A run-length scan over binary data needs a shuffled control or it means
nothing.** Add one before trusting any pattern found this way.

### What IS still true

- The float values present in the records are exact binary fractions
  (`0.6015625 = 77/128`), so **some** quantity in there is quantised.
- `c20` records carry **RGBA colours** past the ten pointers — every one starts
  `0xFF` (alpha 255). Verified on `escf3` and `205a1N`.
- The structural side (container, header, array offsets, strides, trailers,
  texture indices, pointer relocation, case folding) is **solved and
  code-verified**. None of that is in doubt.

### The next move

**Stop scanning. Disassemble the render path.** The arrays are read by code
after load — find the function that consumes the `c14`/`c10` base pointers and
read the *stride it walks with* and the *conversion it applies*. The bytes will
not tell you the encoding. The instructions that decode them will.

---

## ★ A REAL packed int8 stream — found, controlled, not yet named

**Correcting the section above.** The "methods that failed" entry for the packed
stream was wrong, and the control is what proved it:

```
longest int8 run, real 205a1N.c3d :  1149
longest int8 run, SHUFFLED bytes  :    23
```

**50:1. It is not noise.** The sliding-window pattern I dismissed was a window
sliding over something that genuinely exists.

### The stream

```
offset   136,674  ..  140,121      3,447 bytes
1,149 triples of signed int8 (x, y, z)
x: min -128   median 0   max 123
y: min -128   median 0   max 123
z: min -128   median 0   max 123
```

**Sharp edges on both sides — the run length falls to 0 one byte earlier and
drops off one byte later. Real field boundaries, not a gradient.**

Sandwiched between two arrays of `u32 = 1`:

```
before:  01 00 00 00  01 00 00 00  01 00 00 00 ...
after:   00 00 00 01  00 00 00 01  00 00 00 01 ...
```

### The structure inside it

**Every triple has its non-zero components equal to each other:**

```
  1   0 100
100   0   0
 82  82   0
  0  72  72
  0   0 105
105   0   0
 73  73   0
  0  87  87
```

**And the axes cycle in a fixed order:**

```
Z  ->  X  ->  XY  ->  YZ  ->  Z  ->  X  ->  XY  ->  YZ  ...
```

This is a structured stream of direction vectors. **What is not established is
what they are** — the magnitudes run 68–116, not a clean 127, so they are *not*
simply unit normals scaled by 127.

### Standing method rule from this

**Run-length scans over binary data require a shuffled negative control.** This
one was run late, after the finding had already been written off — and it
reversed the call. **Run the control first, not as a post-mortem.**

---

## ★ c14 IS A FLOAT ARRAY — and the 52-byte stride is WRONG

Three findings, all controlled.

### 1. The .c3d files are GZIP on disk

```
first 32 bytes:  1f 8b 08 00 00 00 00 00 ...
                 ^^^^ gzip magic
```

`205a1N.c3d` is **27,084 bytes on disk, 165,636 decompressed.** The loader at
`c3d.py` already knew this — `gzip.decompress(raw) if raw[:2]==b'\x1f\x8b'`.

**Consequence, and it cost a whole round:** scanning the *raw* file for byte-run
structure found enormous fake runs in every file, because compressed data is
high-entropy by definition. **Any structural scan must call `load()`, not
`open().read()`.**

### 2. c14 holds floats

High-byte histogram of the c14 block — this is a float exponent distribution
and nothing else produces it:

```
0x3f  x4772    1.0
0x3e  x2407    0.5
0xbf  x1566   -1.0
0xbe  x1300   -0.5
0x3d  x785     0.25
0xbd  x291    -0.25
0x3c  x264     0.125
```

12,895 of 15,652 values are finite and in ±10; 96.3% inside [-1, 1];
range ±1.9085.

### 3. The 52-byte stride is an assumption that fails

Sliced at 52 bytes, all thirteen columns come back **statistically identical**:

```
fld 0   min -1.9061  med 0.1922  max 1.9085  26.1% neg
fld 1   min -1.9061  med 0.1872  max 1.9085  25.1% neg
...
fld 12  min -1.9085  med 0.1808  max 1.9085  24.5% neg
```

Identical columns mean the stride is **not** a record boundary — it is slicing
one flat buffer uniformly, so every column samples the same distribution.
**c14 is 62,608 bytes = 15,652 consecutive floats, one type.**

### 4. Spatial coherence points at a 24-byte stride

Mean distance between consecutive 3-float groups vs the same groups shuffled:

```
stride  6 floats (24 B):  consec 0.3658   shuffled 1.4169   ratio 3.87x
stride 12 floats (48 B):  consec 0.3939   shuffled 1.4816   ratio 3.76x
stride  3 floats (12 B):  consec 1.2292   shuffled 1.2775   ratio 1.04x
stride 13 floats (52 B):  consec 1.1367   shuffled 1.2760   ratio 1.12x
```

Only 24 and 48 show coherence. That is the fingerprint of connected geometry.

**BUT: rendering the stride-48 extraction gave min 0.000 / max 1.000 exactly on
all three axes, and a vision pass read the top view as a straight diagonal
line.** Neither is a car. Something about the grouping is still wrong.

**NEXT: autocorrelation/FFT on the float buffer to find the TRUE period before
guessing another stride.**

---

## ★ c14 SOLVED to the record level: 12 floats / 48 bytes

Two independent methods agree, and the arithmetic closes exactly.

### The period

Autocorrelation on the RAW float sequence (sentinels zeroed, **nothing filtered
out** — the earlier "period 10" was an artifact of dropping elements):

```
lag 12 ( 48B)  +0.863  ★
lag 24 ( 96B)  +0.771
lag 36 (144B)  +0.695
lag 48 (192B)  +0.636
lag 60 (240B)  +0.587
everything else  < 0.13
```

Sentinel spacing, measured independently:

```
gap 12  -> 859 times   ★
```

### The arithmetic

```
c14 block        77,508 ..  140,116     62,608 B
1304 records x 48 B          =  62,592
starts at 77,524  +  62,592  =  140,116   ★ EXACT
```

The block is 16 bytes longer than 1304x48, so the data begins 16 bytes in.
**The earlier coherence sweep had already peaked at offset 77,524 before I
understood why.**

### The 12 fields

```
 f   min       med       max     neg%   shape
 0  -1.0000   0.0000    0.9997   39.6%   signed ±1
 1  -1.0000   0.0000    1.0000   39.1%   signed ±1
 2   -- see below --                        SENTINEL in  345/1304  (26.5%)
 3   -- see below --                        SENTINEL in 1204/1304  (92.3%)
 4   0.0000   0.5938    0.9998    0.0%   unsigned 0..1
 5   0.0000   0.5939    0.9999    0.0%   unsigned 0..1
 6   0.0000   0.5938    0.9998    0.0%   ★ identical to f4
 7   0.0000   0.5939    0.9999    0.0%   ★ identical to f5
 8  -1.9085   0.0000    1.9085   49.8%   broad
 9  -0.5566   0.0000    0.7229   40.1%   signed ±1
10  -0.8791   0.0174    0.8791   41.9%   signed ±1
11  -1.0000   0.0000    1.0000   44.6%   signed ±1
```

**f4 == f6 and f5 == f7 EXACTLY, in all 1304 records.** Two floats stored twice.

### What this rules OUT

**Not a plain vertex buffer.** A vertex does not store the same two values
twice, and it does not hold fields constant across records (`f10` is
-0.13779 in consecutive records; `f1` sits near -0.5816 for long stretches).

**Not 24-byte vertex pairs either** — the coherence peak at 24 was real but the
48-byte periodicity is stronger and the field structure only resolves at 48.

### What it probably IS

Some per-record descriptor with an embedded duplicate pair — a transform,
a bounding/plane entry, or an animation/key entry. **Not yet named. Do not
guess.**

**NEXT: correlate c14 records against c20 (888 nodes) and c18 (15 records).
If c14 is 1304 and c20 is 888, the ratio 1304/888 = 1.468 has to mean something.**


---

## ★ CORRECTION to the field table above

**The sentinel census was misread when this was first written.** Exact counts:

```
f 0: sentinel    0/1304
f 1: sentinel    0/1304
f 2: sentinel  345/1304   real 959     (26.5% null)
f 3: sentinel 1204/1304   real 100     (92.3% null)
f 4 .. f 11: sentinel 0/1304
```

**Only f2 and f3 ever hold a null. f3 is null 92% of the time — it is a
mostly-empty field, not a data column.**

**And f2 is *not* mostly-null: 959 of 1304 records carry a real value, which the
earlier shape table discarded because the field values are large.**

### What f2 actually contains

```
ff 64 9a 9a
ff 52 ac ac
ff 48 b5 b5
ff 69 69 95
ff 49 4a b3
ff 57 57 a7
ff 51 51 ac
ff 44 44 b7
```

**Every sampled value has `ff` as its leading byte.** That is not a payload byte
— it is the sentinel's high byte bleeding across my field boundary. **The record
offset is wrong by one byte for at least this column**, which also explains the
equal byte-pairs (`9a9a`, `acac`, `b5b5`, `5757`, `5151`, `4444`) seen here and
four rounds ago in the "int8 stream".

### Also found

`(f0, f1, f11)` has **median length exactly 1.0000**, p25 0.973, p75 1.021.
Three fields forming a unit vector. **They are not contiguous, so the field
order is not the storage order** — which is consistent with the one-byte offset
problem above.

`(f0, f1)` traces with a mean angle step of 0.912 rad (52.3°) — consistent with
something sampled about 7 times per revolution, not a dense circle.

### Method failure to record

**Third filter error of the session, same shape each time:** a threshold is set,
it silently removes a subset of the data, and the remainder is then read as the
whole truth. Specifically here: a `|v| < 100` "sane" filter deleted every real
value in f2, and the resulting `0.0000 .. 0.0000` range was reported as fact.

**Report `n_discarded` next to every filtered statistic.** A column that is
mostly *excluded* tells you nothing about that column.

---

## The offset theory was WRONG — tested and rejected

I hypothesised the `ff` leading byte in f2 meant my record offset was one byte
out. **Tested, and it is not.**

```
clean FFFFFFFF sentinels by 4-byte phase:
   phase 0:  345      phase 1:  0      phase 2:  0      phase 3:  0

record-start shift -3..+3, count of records whose f2 high byte is 0xff:
   shift +0:  1204    ← the true alignment
   shift +1:     0
   shift +2:     0
   shift +3:     0
   shift -1:   346
   shift -2:   348
   shift -3:   346
```

**Shift +0 is correct: 345 clean sentinels and the f2-`ff` signature is maximal
there. Shifting by one byte destroys both.**

So the `ff` is **real data**, not a boundary artifact. Reading the whole record
at +1 gives nonsense across every column (all twelve collapse to
`med 0.0000`, `max 32.0003`), which is what a misalignment *looks* like —
**further proof that +0 is the right one.**

**What `ff` as a leading byte in f2 means is still unknown.** A float with a
leading `0xff` is a large negative number; a `u32` in that range sits above
`0xff000000`. Neither has been ruled in or out. **Not guessing.**

---

## ★★ c14 IS THREE SEGMENTS — and 1204 is a BOUNDARY, not a record count

Record-level structure, exact and gap-free:

```
records    0 ..  343   (344)   f2 null    f3 null
records  344 .. 1203   (860)   f2 REAL    f3 null
records 1204 .. 1303   (100)   f2 REAL    f3 REAL

         344 + 860 = 1204
```

**The nulls are not scattered. f3 is null in ONE unbroken run, records 0–1203.
f2 is null in one unbroken run, records 0–343. Then each switches on once and
never switches back off.**

```
sentinel run lengths:
   f3:  a single run of 1204
   f2:  a single run of 344
```

### What `1204` actually is

The header field at `+0x14` reads **1204**, and the loader allocates
`count × 13 × 4` = `1204 × 52` = **62,608 bytes** — which is exactly the block
size, and the whole-file identity depends on it.

But the data is **48 bytes per unit**, and `1304 × 48 = 62,592`, leaving 16 bytes
of slack inside the allocation.

**1204 is the index where the third segment begins, not the number of records.**
Everything from 1204 to 1303 is the 100-record tail the loader over-allocates
for, and that is the 16-byte discrepancy that made the stride look wrong from
the beginning.

### Cross-model confirmation

Same period, same value, in three unrelated cars:

```
205a1N.c3d  c14=1204  period 12 floats (48 B)  r=+0.863
206a1N.c3d  c14=1147  period 12 floats (48 B)  r=+0.842
cora1N.c3d  c14=1088  period 12 floats (48 B)  r=+0.846
```

**This is the format, not a quirk of one model.** Any explanation has to hold
across all three.

### Still open

What the three segments *mean*, and what f2's leading `ff` byte encodes.
**Not guessed.**

---

## ★★★ THE c14 BLOCK IS TWO PARALLEL ARRAYS — verified 23/23

The "52 versus 48" discrepancy is solved, and it was never a stride error.

```
c14 block = n52 x 52 bytes, structured as:

   n52 x 48 bytes     FLOAT DATA       (n52 units of 12 floats)
   n52 x  4 bytes     PARALLEL ARRAY   (n52 u32 values)
```

**Both are exact multiples of n52. No padding, no slack, no 16 stray bytes.**

Cross-model check, all 23 `*N.c3d` models:

```
205a1N n52=1204   tail starts at c14b + n52*48   ✓
206a1N n52=1147   ✓      6r4a1N n52=1360  ✓     cora1N n52=1088  ✓
esca1N n52=1132   ✓      suba1N n52=1116  ✓     ... 23/23
```

### The tail array

1204 u32, only 107 distinct values. Top of the histogram:

```
0x00010000  x340      0x00008080  x207      0x00005757  x29
0x00004444  x23       0x00004242  x23      0x0000a5a5  x21
```

**Read as u16, every value is a byte pair: `0x0000BBBB`.** The byte is duplicated
into both halves. As u16 the whole array is `0x0000 × 1203` and `0x0001 × 343` —
it is a **byte stream expanded into 32-bit slots.**

### ★ And it holds the same data as f2

```
tail[340] = 0x00006464        f2[344] carries byte 0x64
tail[341] = 0x00005252        f2[345] carries byte 0x52
tail[342] = 0x00004848        f2[346] carries byte 0x48
tail[343] = 0x00006969        f2[347] carries byte 0x69

active range  tail: 340 .. 1203
active range  f2:   344 .. 1203
```

**Same byte sequence, stored twice, 4 indices apart.** That is why f2's leading
byte never made sense as a float: **f2 is not a float either.**

### What this retroactively explains

The "int8 stream" found at offset 136,674 earlier in this work — the equal
byte-pairs `9a9a`, `acac`, `b5b5`, `5757`, `5151` that I could not name and
twice wrote off — **is this same byte-expansion. It was never a separate
structure. It was this array, read at the wrong width.**

### Still open

What the byte values *mean*, and why the data is stored both inside the float
records (f2) and again as a parallel array. **Not guessed.**

---

## ★★ BOTH ARRAYS BREAK AT INDEX 344

The tail array and the f2 field of the float records share one boundary:

```
tail array:        344 zeros,  then 860 non-zero bytes
f2 in the records: f2 == null for   344 records, then real for 860
                   (single zero-run, verified — not scattered)
```

**Two separate arrays, identical break point, identical counts. That is a
structural fact of the file, not an inference.**

### The tail byte stream is a SIGNAL — and the normal hypothesis is dead

Adjacent-byte differences:

```
real:      |delta| median  2    mean 13.6
shuffled:  |delta| median 68    mean 74.7        →  5.49x
```

**5.5× smoother than random. This is a correlated, ordered stream.**

**Tested as packed int8 normals (values biased by 128, three bytes per vector)
and it FAILS — and the failure is clean:**

```
real bytes:      median triple length 0.6621    within 5% of 1.0:  2.5%
SHUFFLED bytes:  median triple length 1.0583    within 5% of 1.0: 11.7%
```

**The shuffled control scores *higher* than the real sequence.** Random bytes
make better unit vectors than these. That decisively rules out normals, and it
is only visible because the control was run in the same breath as the test.

### Where f2 fits

f2's leading `ff`, which I spent two rounds trying to explain as an offset
error, is simply this: **f2 is not a float.** It carries the same byte stream as
the tail array, four indices apart.

### Open

What the byte values encode. Distribution is centred on 0x80 with 227 distinct
values and strong local correlation — consistent with a quantised smooth
quantity (angle, curvature, or a normal rotated into a record-local frame).
**Not guessed.**

---

## ★ (f0, f1) RENDERS AS A CIRCLE — the normals are confirmed geometrically

Rendered every plausible field pair as a scatter plot and looked at it, rather
than probing statistically.

**`f0 / f1`: a strong circular ring, with faint cross/spoke structure.**

This is the geometric consequence of a measurement already in hand:
`(f0, f1, f11)` has median length **exactly 1.0000**. If those three form a unit
vector, then `(f0, f1)` is its projection to the XY plane — which *must* form a
disc, and forms a **dense ring** when the vectors cluster near a plane
(`f11 ≈ 0`, i.e. mostly-vertical normals).

**So the float record contains a normal in fields 0, 1, 11.** Not a guess: the
ring is what the unit-vector measurement predicts, and the render matches.

**Note the field order — `0, 1, 11` — is not contiguous.** Whatever the record
layout is, the semantic groups are not stored in simple runs.

### Render artefacts

`/root/cmr2_pairs.svg`, `/root/cmr2_pairs.png` — 11 field pairs, 1204 records.
Only `f0/f1` has been read in detail. **The other ten panels are unanalysed.**

---

## THEATER MODE — feasibility, and what the data gives us

Miami asked whether a replay-mode toggle (pause / step / slowmo / freecam) is
possible. **Yes — and a native port makes it EASIER than the original could,
for one structural reason.**

### Why it is easy: in a replay, nothing is simulating

A freecam during live gameplay is dangerous: the camera matrix is read by AI,
collision and HUD code, so overriding it breaks the sim. **In a REPLAY the
simulation is already over** — CMR2 has recorded the run. Nothing is advancing,
nothing is checking the camera. So pausing, stepping, scrubbing and flying a
free camera are all operations on a frozen stream.

That is the whole architectural argument, and it puts the theater in the right
place instead of bolted onto gameplay where it would fight the physics.

### Cost per control

```
pause            free       stop advancing the tick
step frame       free       advance the tick once
slow / fast      free       scale dt
scrub timeline   cheap      index into a stored stream - NO determinism needed
freecam          medium     set the camera matrix directly
rewind (live)    OUT        needs determinism or snapshots - not attempted
```

**Rewind is the one that is genuinely hard, and it is only hard for the LIVE
sim. Scrubbing a recorded replay is just arithmetic.**

### What the data actually gives us  ★ NEW

```
.rpl files are GZIP-wrapped CMPR containers
   champend.rpl   231 bytes on disk -> 25,374 decompressed
   magic after gunzip: "CMPR"

Stage containers are ALSO gzip-wrapped:
   Swe01Lo.bfl   1,923,469 -> 11,254,304

★ THE STAGE CONTAINER HAS NAMED SHAPE ENTRIES:
   c3dShp0 .. c3dShp56   (57+ of them)
```

**The stage geometry is indexed by name, the same pattern as the car's
260-byte texture path table. That is the track mesh, addressable.**

### Caveat, stated plainly

**The existing `.bfl` parser produces garbage entry names for STAGE files** —
it works on car containers (581/581 byte-identical) but the stage layout differs.
The `c3dShp` names above were read out of raw bytes, not from the parser.
**Stage containers need their own reader before the road can be extracted.**
