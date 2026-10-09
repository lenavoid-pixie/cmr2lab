# How a car is put together

Read off the retail files, then confirmed by matching one against another.

## The family

Every car is a family of files sharing a prefix — the "plate":

```
205.cin                        one definition
205a1N.c3d   205a1.bfl         ┐
205a5.c3d    205a5.bfl         │
205c1.c3d    205c1.bfl         │
205c3.c3d    205c3.bfl         │  ten geometry/texture PAIRS
205c5.c3d    205c5.bfl         │
205d1.c3d    205d1.bfl         │
205d3.c3d    205d3.bfl         │
205e3.c3d    205e3.bfl         │
205f3.c3d    205f3.bfl         │
205f4.c3d    205f4.bfl         ┘
```

Naming: `<car><variant><n>.c3d` — a letter and a number. 259 `.c3d`, 220 `.bfl`,
22 `.cin` across the game. Twenty-three `.c3d` carry a trailing `N`, all of them
on the `a1` variant. What `N` means is **not established**.

## Geometry and textures are linked BY NAME, not by pointer

A `.c3d` does not contain textures. It contains **paths to them**, and those
paths carry the original Codemasters build directories:

```
U:\Cars2\Escort\FEs\Hier\UsePC\FESGlODf
U:\Cars2\Escort\FEs\Hier\UsePC\fesglobu
U:\Cars2\Escort\FEs\Hier\UsePC\FESBodDf
U:\Cars2\Escort\FEs\Hier\UsePC\fesbodbu
U:\Cars2\Escort\FEs\Hier\UsePC\DEsLigBr
U:\Cars2\Escort\FEs\Hier\UsePC\desligru
```

Six of them, in a table at the end of the file: **260 bytes per entry**, exactly
`1560 / 260 = 6.00`. That is the second-to-last region of the payload; the
`Texture 0..5` name table sits immediately before it, as **24-byte records**.

The requested names match the `.bfl` contents **6/6**, and note the case:

```
.c3d asks for   FESGlODf   →  .bfl provides   fesglodf
.c3d asks for   DEsLigBr   →  .bfl provides   desligbr
```

**The lookup is case-insensitive.** Mixed case in the geometry, lowercase on
disk.

## Why this matters

Because the two halves are joined by a *name* and not a pointer, they are
independent:

- new textures, same geometry — unpack the `.bfl`, replace the DDS, repack
- new geometry, same textures — write a new `.c3d` naming the textures you already have
- and a `.c3d` can name textures that live in a *different* container

That last one is the door a converter walks through.

## The one real constraint

Geometry is free to change. **Physics is not.** The collision model is built
from `halfExtents` plus four wheel positions and never reads the mesh — so a
new body has to keep its **wheelbase and track** inside the values the car's
definition already declares, or the wheels will sit off the box.

Everything else about a model swap is the engine running normally.

## The header

```
+0x00  "PP_F"
+0x04  u16  count          (6 for a car)
+0x06  u16  0
+0x08  u32  offset
+0x0C  u32  offset
+0x10  u32  offset
+0x14  u32  offset
+0x18  u32  node type      e.g. 0x00020003 -> u16 pair (3, 2)
+0x20  u32  node type      e.g. 0x00010006 -> u16 pair (6, 1)
```

Node type markers are u16 pairs. `0x00020003` recurs across cars and appears to
mark wheels; `0x000E000F` marks view nodes. The loader in `CMR2.exe` guards on
vertex count — the string `"Requested Vertices : %d  Limit : %d"` sits directly
above the `PP_F` literal in the binary.

**The record layout between the header and the trailing tables is still open.**
That is the last unknown standing between here and a converter.
