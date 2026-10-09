# The `PP_F` geometry format

Shared by **cars and tracks**. Recovered from the loader at
`CMR2.exe + 0x004B93C0` — the function the `"PP_F"` string compare at
`+0x004B9389` calls when the magic matches.

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
