# CMR2LRE

**Colin McRae Rally 2.0 — Linux Rally Edition.**
Running the 2000 PC rally game natively on Linux. No Wine. No Proton. No compatibility layer.

> **This is not a finished game and it is not playable yet.** Read the status table
> below before you download anything — it will take you ten seconds and save you an
> afternoon. Everything else in this file is written for two kinds of people, and
> both are labelled.

---

## What is this, in plain words

In 2000, Colin McRae Rally 2.0 came out on PC. It only ever ran on Windows. Twenty-six
years later it still only runs on Windows — or through Wine and Proton, which are
translators that pretend to be Windows so the game doesn't have to change.

This project is the other route: **change the game so it speaks Linux directly.**

That is a big job, and it is going well. The game's source code has been recovered from
the shipped `.exe` — 122 files, 135,805 lines of C++ — and **half of it already compiles
natively on a Steam Deck.** The renderer is being rewritten. It draws a car from the
retail game files onto your screen using a real Vulkan window.

It does not yet draw a *track*, a *car you can drive*, or a *menu you can click on*.
That is what's left.

Separately — and available **right now** — this repo also contains tools that open the
game's data files: pull textures out of it, look at them, change them, put them back,
with a byte-for-byte guarantee that your rebuilt file is indistinguishable from one the
game shipped. That part works today.

---

## Can I use it? (the honest table)

| What you want to do | Works today? | Where |
|---|---|---|
| **Look inside the game's data files** | ✅ **Yes, right now** | `./cmr2lab` — see below |
| **Pull out a car's textures as normal images** | ✅ **Yes, right now** | `./cmr2lab unpack` |
| **Edit a texture and put it back in the game** | ✅ **Yes, right now** | `./cmr2lab pack` |
| **Read the game's 3D model format** | ✅ Documented | `docs/C3D.md` |
| **See a car from the game drawn natively** | ⚠️ Yes, but it's a *viewer*, not the game | `src/port/` |
| **Half the game's code compiling for Linux** | ✅ 33 of 66 files | `docs/PORT-PLAN.md` |
| **The other half compiling** | ❌ Not yet — pointer-width work | `docs/P3-P1-REPORT.md` |
| **Driving a rally car** | ❌ **No.** Not yet | — |
| **Menus, tracks, stages, championship** | ❌ No | — |
| **A one-click installer** | ❌ Not yet — that's the destination | — |

If you came here to *play Colin McRae Rally 2.0 on Linux*, the honest answer today is:
**use Proton, it works fine.** Come back here later. This page will say so when it's time.

---

## Track 1 — "I have a Steam Deck and I've never used Desktop Mode"

You need: a Steam Deck, and a copy of Colin McRae Rally 2.0 for PC (GOG or original
disc — the files must be on your machine; **this project ships none of them**).

**First, get to Desktop Mode.** Hold the **power button** and choose **Switch to
Desktop**. The Deck turns into a normal computer with a mouse. You can switch back the
same way. A keyboard and mouse help a lot, but the touchscreen and trackpads work.

**Open the terminal.** In Desktop Mode there's an icon on the taskbar at the bottom
called **Konsole** — a black square with a `>_` in it. Tap it. A black window opens
with a blinking cursor. That's a terminal: you type a command, press Enter, it does
the thing. Nothing you type here can break your Deck.

**Copy and paste this, one block at a time.** Tap the line, then paste, then Enter.
(If you have no keyboard: long-press the terminal and pick *Paste*.)

```
cd ~
git clone https://github.com/LENAWare-Works/cmr2lab
cd cmr2lab
chmod +x cmr2lab
```

The first command puts you in your home folder. The second downloads this project into
a folder called `cmr2lab`. The third goes into it. The fourth makes the tool runnable.

**Now find where your game is.** If you don't know, this finds it for you:

```
find / -iname "Colin McRae Rally 2.0" -type d 2>/dev/null
```

It'll print a path. Use that path everywhere below.

**Now try it:**

```
./cmr2lab status /the/path/it/printed
```

You should see a count of containers and a list of what's inside. If that worked,
you're done — you can now use every command in the table below.

**If something went wrong:** screenshot it and open an issue. There is no such thing as
a stupid question here, and "it didn't work" is a completely valid bug report.

**When the GUI installer arrives, this whole section gets replaced by one download.**
That is the plan and it is why the manual steps are kept in one tidy block.

---

## Track 2 — "I know what a terminal is"

```
git clone https://github.com/LENAWare-Works/cmr2lab && cd cmr2lab
./cmr2lab status   /path/to/Colin\ McRae\ Rally\ 2.0
./cmr2lab list     /path/to/game cars
./cmr2lab unpack   /path/to/game/Game/Cars/205a1.bfl /tmp/205
```

One file. Python 3 standard library only. No dependencies, no install, no virtualenv.

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

**The guarantee:** every `.bfl` in the retail game round-trips **byte-identical**.

```
$ ./cmr2lab repack /path/to/Colin\ McRae\ Rally\ 2.0
repack VERIFY: 581/581 byte-identical   (skipped 1 non-container)
```

2.26 GB, 581 containers, zero failures. If a container rebuilds to the exact original
bytes, then a *modified* container is structurally indistinguishable from something the
game shipped. That's the whole safety argument for editing your own copy.

---

## Track 3 — "I want to build the native port"

Read `docs/PORT-PLAN.md` first. It has the measurement method, the error census, and an
explicit list of numbers this project published before running them and then had to
withdraw — read that list before you quote anything.

**What compiles today:** 33 of 66 translation units on `x86_64-linux-gnu`; 66 of 66 on
`i386-linux-gnu`. The i386 build is kept deliberately as a **diagnostic oracle**: a file
that fails on x86_64 but passes on i386 is blocked by pointer width and nothing else.
Right now that's all 33 of them — every remaining error is `cast from pointer to smaller
type 'int'`.

**The entire remaining Windows surface:**

```
278  Direct3D 7 call sites, 20 methods — 256 state, 22 draws
 45  files that include <windows.h>
 36  __asm occurrences — 0 visible to a non-MSVC compiler
```

81% of the D3D7 surface is in `Graphics.cpp` alone. The ratio matters more than the
total: **22 draws against 256 pieces of state** is the entire architectural problem,
because modern graphics APIs have no equivalent of "set this state, now draw" — you
declare the whole state up front and get a pipeline handle back. That means a state
tracker, which is the real mountain.

**One genuinely good discovery:** the game is Direct3D 7 **fixed-function** and submits
`D3DTLVERTEX` — transformed, lit, screen-space triangles. There are no shaders to port.
The game already does its own transform and lighting math in software. The renderer's job
is "fill screen-space textured triangles", which is close to the most portable shape 3D
code can have.

**Toolchain on a Deck (rootless, no sudo):** `zig cc` with standalone cmake and ninja, or
a distrobox container. Both are documented. Note that `zig` defaults to musl — you must
pass `-target x86_64-linux-gnu`.

---

## Where this is going

In rough order, and every item here is a real plan rather than a wish:

1. **Get the game to draw** — the state tracker / RHI, the last 33 files compiling.
2. **The GUI installer** — one download, one click, no terminal. The destination.
3. **Controller support** — analogue triggers that behave progressively, because a rally
   game with on/off throttle isn't a rally game.
4. **A championship builder** — Classic mode (the original progression) or Custom: any
   number of tracks, reversed or not, stages per track, repairs on or off.
5. **Damage on a slider** — the game already deforms real mesh vertices on impact;
   Classic keeps the original values, or you scale them up.
6. **Multiplayer** — and the good news is that rally is asynchronous. Cars race the
   clock, not each other, so this needs a shared championship file and submitted stage
   times, not lockstep netcode. Daily and weekly challenges ride on the same idea.
7. **CMR3 content** — opens once the `PP_F` record layout is parsed for its own files.

---

## Where everything lives

| path | what |
|---|---|
| `cmr2lab` | the container tool — one file, no dependencies |
| `src/port/` | the native renderer: C + SDL3 + Vulkan |
| `src/viewer.c` | draws game models outside the game |
| `docs/CONTAINER.md` | the `.bfl` container format |
| `docs/C3D.md` | the `PP_F` 3D geometry format |
| `docs/PORT.md` | the native port, overview |
| `docs/PORT-PLAN.md` | measurement, error census, withdrawn numbers |
| `docs/P3-P1-REPORT.md` | what changed and what it moved |
| `docs/GLOSSARY.md` | **every term in this repo, in plain English** |
| `docs/feedback/` | Miami's own eyes on what the port looks like |
| `screenshots/` | frames the port has actually rendered |
| `tools/` | the analysis scripts behind the numbers |
| `progress.md` | the live working log |

**New here? Read `docs/GLOSSARY.md` first.** If a word in this README was unfamiliar,
it's defined there — D3D7, RHI, translation unit, triangle list, byte-identical, all of it.

---

## A note on the name

The repository is called `cmr2lab` because it started as a container-inspection lab.
The project it now serves is **CMR2LRE**. The old name stuck to the URL; the project
outgrew it.

---

## Legal

This repository contains **no game data.** No textures, no models, no audio — not one
byte from Colin McRae Rally 2.0 or 3. It is a tool.

Bring your own copy of the game. Reverse engineering for interoperability is established
practice; redistributing the assets is not, and this project does not do it. The same
rule that keeps decompilation projects alive for decades is the rule here: **ship the
code, let the user supply the files.**

Everything in this repository is code and documentation, and the geometry, container and
texture work was derived from data the user already owns.

MIT licensed — see `LICENSE`.

---

## Who builds this

Miami owns it. The work is done by two instances of the same agent — one on a Steam Deck,
one on a phone — which is why the commit history has an unusual shape. The Deck one does
the port work; the phone one does the archaeology and writes things down.

Both of them read the actual files rather than remembering what's in them, which sounds
like a low bar until you watch how often it matters.
