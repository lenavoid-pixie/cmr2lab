# cmr2lab

A content laboratory for **Colin McRae Rally 2.0** (PC, 2000).

Unpack the game's `.bfl` containers, look inside, change something, put it back.

```
cmr2lab status              what's here, what's possible
cmr2lab list cars|tracks    enumerate content
cmr2lab info   <file.bfl>   entries, magic, breakdown
cmr2lab unpack <in> <dir>   extract a container
cmr2lab pack   <dir> <out>  rebuild one
cmr2lab repack <file|dir>   round-trip and VERIFY byte-identical
cmr2lab tabs                show the CMR2 / CMR3 content tabs
cmr2lab install             write a Steam launcher entry
```

One file. No dependencies. Python 3 standard library only.

## The guarantee

Every `.bfl` in the retail game round-trips **byte-identical**:

```
$ cmr2lab repack /path/to/Colin\ McRae\ Rally\ 2.0
repack VERIFY: 581/581 byte-identical   (skipped 1 non-container)
```

2.26 GB, 581 containers, zero failures. If a container rebuilds to the exact
original bytes, then a *modified* container is structurally indistinguishable
from something the game shipped.

## Try it

```
git clone https://github.com/LENAWare-Works/cmr2lab
cd cmr2lab
./cmr2lab status /path/to/your/game
./cmr2lab unpack /path/to/your/game/Game/Cars/205a1.bfl /tmp/205
ls /tmp/205          # 27 real DDS textures
```

## Content tabs

| tab | state |
|---|---|
| **CMR2** | live today — unpack, repack, replace textures on the original game |
| **CMR3** | opens when the `PP_F` geometry record layout is parsed |

Content from other games goes in its own tab. It never mixes with the original.

## Legal

This repository contains **no game data**. No textures, no models, no audio —
not one byte from Colin McRae Rally 2.0 or 3. It is a tool.

Bring your own copy of the game. Reverse engineering for interoperability is
established practice; redistributing the assets is not, and this project does
not do it. The same rule that keeps decompilation projects alive for decades
is the rule here: **ship the code, let the user supply the files.**

## Format notes

See `docs/CONTAINER.md` for the `.bfl` container layout, derived from real
disc data and verified by the round-trip test above.

## Update — the geometry format is cracked

`docs/C3D.md` documents `PP_F`, shared by cars and tracks.

**259 / 259 car models close exactly** on the identity:

```
48 + sum(count * stride) + TEXCOUNT*24 + TEXCOUNT*260  ==  file size
```

And the same parser reads a **stage road**: `temp.c3d` from `Sweden/Swe01Lo`
closes exactly at 2,894,356 bytes — 16,157 nodes of 76 bytes, 29,399 segments
of 52, 27 textures.

Recovered from the loader at `CMR2.exe + 0x004B93C0`, reached from the
`"PP_F"` compare at `+0x004B9389`.

`N` = body, `L` = left wheel, `S` = right wheel.

**The topology is an indexed triangle list** — settled by the game's own
decompiled loader (`Graphics.cpp:3209` calls `DrawIndexedPrimitiveVB` with
`D3DPT_TRIANGLELIST`), plus statistical, count-parity and render evidence.
See the top of `docs/C3D.md`. A triangle strip was tested, produced a
convincing-looking car, and was **wrong**.

## Update — the native port

Since 2000 the game has only ever run on Windows. `docs/PORT.md` tracks
**compiling the actual game for native Linux — no Wine, no Proton.**

A decompilation of the PC build exists: **122 files, 135,805 lines of C++**.
The entire remaining Windows surface is:

```
278  Direct3D 7 call sites, 20 methods — 256 state, 22 draws
 45  files that include <windows.h>
 36  __asm occurrences, 6 files — 0 visible to a non-MSVC compiler
```

That is the whole distance. `src/port/` holds the native renderer — C + SDL3,
built rootless with `zig cc`, opening a real Vulkan window and drawing a model
straight out of the retail files. It renders a car. It is not a game yet.

**How much of that decompilation compiles today: 26 of 66 translation units** on
x86_64 (53 of 66 on i386, which is kept only as a diagnostic — those 27 extra
files are blocked by pointer width and nothing else). `docs/PORT-PLAN.md` has the
measurement, the method, the error census, and a list of the numbers this
project published before running them and then had to fix.
