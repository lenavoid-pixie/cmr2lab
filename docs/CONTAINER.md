# The `.bfl` container

Everything in CMR2 lives in a `.bfl`. There are 582 of them: 220 car texture
containers, 212 track containers, the rest front-end and audio.

## Outer layer: gzip

Most `.bfl` files on disc are gzip streams (`1f 8b`). Some are stored raw.
Detect, don't assume — the tool handles both.

## Inner layer: CMPR

```
offset  size  field
0       4     magic "CMPR"
4       4     u32 containerSize      == totalLength - 8
8       D     file data, concatenated
8+D     T     TOC entries
L-4     4     u32 tocPointer         == D
```

`L` = total length. The TOC pointer is the **last four bytes of the file**, and
it points at the TOC start minus 8.

## TOC entry

```
u32 size        file size in bytes
u32 offset      data starts at absolute (offset + 8)
u32 strLen      filename length, NOT including padding
char name[strLen]
pad             to a 4-byte boundary
```

## The rule that took two attempts to find

**The entire data area is 4-byte aligned — including between entries, and
including the final pad before the TOC starts.**

Car containers hide this, because DDS and TGA payloads are almost always
already multiples of four. The one that exposes it is a stage container with a
**27-byte `srf`** in it, which leaves a 1-byte gap before the next entry.

```
3,226,479 → 3,226,480      entry start aligned
11,141,150 → 11,141,152    entry start aligned
22,007 → 22,008            TOC start aligned
```

Without those pads a rebuild comes out 7 bytes short. With them, all 581
containers reproduce exactly.

## Payloads inside

| magic | meaning |
|---|---|
| `PP_F` | geometry. **The same magic for cars and tracks.** |
| `DDS ` | DirectDraw Surface texture (DXT5/BC3) |
| other | TGA, data blobs, per-stage logic |

A stage container holds 72 named entries: `temp.c3d` (road), `temp.obj`
(scenery), `temp.sht`, `temp.sky`, `temp.gro`, a 332 KB collision file, a
**27-byte surface table**, a **48-byte spatial index**, 5 MB of trees, and 48
tiny `.hor` files — 12 times of day × 4 weathers, each 80 bytes of colour.

## Credits

The container format was cross-checked against `CMR2Decomp/CMR2Decomp`'s
`BFL.h`, `AdTec224/BFLExtract` and `MarkusMaal/BFLExtractCsharp`. The 4-byte
data alignment and the TOC-alignment rule were derived here from the retail
files and confirmed by the 581/581 byte-identical sweep.
