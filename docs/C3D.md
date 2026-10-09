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
