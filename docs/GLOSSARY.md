# Glossary

**Every term used anywhere in this project, in plain English.**

If you hit a word in the README or in a doc that isn't here, that's a bug in this file —
open an issue and it gets added. This document exists because "the PP_F record stride is
48 bytes" is a perfectly good sentence to someone who already knows what a stride is, and
completely opaque to everyone else.

Three sections: **start here** if you've never used a terminal, then **the game and its
files**, then **graphics and the port**. Skip whatever you already know.

---

## Start here — you've never done this before

**Terminal / console / shell / command line**
A window where you type instructions instead of clicking. It looks intimidating and isn't.
You type a command, press Enter, it prints an answer. Anything you'd do by clicking can be
done here, usually faster. On a Steam Deck it's called **Konsole** and lives on the
Desktop Mode taskbar.

**Command**
One line of text you type into the terminal. Nothing happens until you press Enter, so you
can always back out before committing.

**`cd`**
"Change directory" — the command that moves you between folders. `cd ~` takes you home.

**`~` (tilde)**
Shorthand for your home folder. On a Deck that's `/home/deck`.

**`/` (slash)**
The root of the whole filesystem. Also appears inside paths as a separator between folders,
the same way Windows uses `\`.

**Path**
The address of a file or folder. `/home/deck/cmr2lab` means: start at the root, go into
`home`, then `deck`, then `cmr2lab`.

**Repository / repo**
A folder of code that remembers its own history. Every change anyone ever made is stored,
along with who made it and why.

**Git**
The tool that manages a repository.

**`git clone`**
Downloads a repository from the internet onto your machine, history and all. You do this
once per project.

**Commit**
A saved change in a repo's history, with a message explaining it. `git log` lists them
newest-first.

**GitHub**
The website that hosts repositories. It's where this one lives.

**Issue**
A bug report or a question, filed on GitHub. **"It didn't work" is a valid issue.** So is
"I don't understand this". Neither is a stupid question.

**MIT license**
The legal terms. In plain terms: you can use, change, and share this code, as long as you
keep the copyright notice. See `LICENSE`.

**Rootless / no sudo**
Installing and running software without administrator permission. This matters on a Steam
Deck because much of its system is read-only on purpose.

**Steam Deck / Desktop Mode**
The Deck boots into a console-style interface. **Desktop Mode** turns it into an ordinary
computer with a mouse cursor — hold the power button and pick *Switch to Desktop*. Same
button switches back.

---

## The game and its files

**Colin McRae Rally 2.0 (CMR2)**
A rally racing game released for PC in 2000. Windows only. The subject of this project.

**CMR2LRE**
*Colin McRae Rally 2.0 — Linux Rally Edition.* The name of the project: making that game
run natively on Linux.

**cmr2lab**
This repository. Started life as a tool for inspecting the game's data files and kept the
name after the project grew past it.

**Container / `.bfl` file**
A single file that the game uses to bundle many other files together — a car's textures, a
track's data, all packed into one blob. Think of a `.zip`, except it's the game's own format
and nobody published how it works. **This project worked out how it works** (`docs/CONTAINER.md`)
and can break one apart and put it back together again perfectly.

**Byte-identical**
Exactly the same, byte for byte — not "looks the same", *is* the same. When this project
says a container repacks byte-identical, it means the output file has the same length and
every single byte matches the original. That's the safety proof: a file you edited this way
can't be distinguished from one the game shipped.

**Round-trip**
Take something apart, put it back together, and check it came out unchanged. The round-trip
test here covers all 581 containers in the retail game.

**CMPR**
A compression method the game uses inside its files. Looked like a wall for a while; it's
cracked, and it is not a standard compression format.

**DDS / DXT5**
The texture formats the game uses. A `.dds` is a plain image file that Photoshop or
GIMP can open, so once textures are unpacked you can edit them like any picture.

**`.c3d`**
The game's 3D model format — a car body, a wheel, or a piece of scenery.

**`PP_F`**
The four bytes at the start of a `.c3d` file that identify it, in the same way `PNG` starts
a PNG. Recovered from the game's own loader, which compares those bytes to decide how to
read the rest.

**`N` / `L` / `S`**
In filenames like `206a1N`, the letters mean **N** = body, **L** = left wheel,
**S** = right wheel. They're separate models, which is why a wheel can in principle be
knocked off.

**The tabs**
The tool shows content grouped into tabs: **CMR2** is the original game; **CMR3** is
Colin McRae Rally 3, whose files this project can partly read. Content from other games
never mixes with the original.

---

## Graphics and the port

**Native**
Running as the operating system intends, with no translation layer in between. A native
Linux build is a Linux program, not a Windows program wearing a costume.

**Wine / Proton**
Compatibility layers that let Windows programs run on Linux by pretending to be Windows.
Proton is Valve's version, built into the Steam Deck. **They work well and this project is
not a criticism of them** — it's a different route to the same destination.

**Decompilation / decomp**
Recovering readable source code from a compiled program. A `.exe` is machine code, which
humans can't read; decompiling turns it back into C++ that people can. Legally, doing this
for interoperability is established practice. **The decompilation of CMR2's PC build exists
and is the foundation of this port** — 122 files, 135,805 lines.

**Translation unit**
Roughly, one source file. When someone says "33 of 66 translation units compile", it means
33 of the 66 `.cpp` files in the project build successfully. It's the standard unit for
measuring progress on a port, because a file either compiles or it doesn't.

**x86_64 / i386**
Two versions of the same processor family. **x86_64** is 64-bit and is what your Steam Deck
runs. **i386** is 32-bit and is what the game was originally built for. The difference
matters enormously below.

**Pointer width**
The size of a memory address. On a 32-bit system a pointer is 4 bytes; on a 64-bit system
it's 8 bytes. The original game was written assuming 4, and half its structs quietly
assume that too. **This is the single biggest remaining job in the port** — over a thousand
places where an address gets squeezed into a 4-byte integer and loses information.

**LP64 / LLP64**
Naming schemes for which types are how big on a given system. They differ in one nasty way:
on Linux a `long` is 8 bytes, but on Windows a `long` is always 4. Code that says
`typedef long LONG` is therefore correct on Windows and silently wrong on Linux. That exact
one-line bug sat in this project's platform header and was found and fixed on 2026-10-10.

**Win32 / `<windows.h>`**
The Windows programming interface. 45 files in the game use it. Replacing those calls with
Linux equivalents is a large but mechanical part of the port.

**`__asm`**
Hand-written assembly language buried inside C++ code. Usually a porting nightmare — but
here, all 36 occurrences sit inside `#ifdef _MSC_VER` blocks with portable fallbacks, so
none of them are actually visible to a Linux compiler. A rare piece of good luck.

**Direct3D 7 / D3D7**
Microsoft's 1999 graphics API. What the game uses to draw. Long obsolete, which is why it
has to be replaced.

**Fixed-function**
A graphics pipeline where you describe *what* to draw and the hardware does the math for
you, with no custom programs. The alternative is **shaders**.

**Shader**
A small program that runs on the graphics card and computes what a pixel looks like.
Modern graphics is entirely shaders. **This game has none** — which is genuinely good news,
because it means there's no shader code to rewrite. See `D3DTLVERTEX`.

**`D3DTLVERTEX`**
"Transformed and lit vertex" — a vertex that has *already* had all its 3D math done, leaving
plain screen coordinates. The game computes all of its own geometry transforms in software
and hands the graphics card finished 2D triangles. It means the port only has to fill
screen-space triangles with texture and colour, which is about the simplest rendering job
there is.

**Vertex**
A corner point of a 3D shape. A triangle has three; a car has thousands.

**Triangle list vs. triangle strip**
Two ways of telling a graphics card which vertices make which triangles. A **list** spells
out every triangle's three corners. A **strip** implies them from their neighbours and
wastes fewer vertices. **This project got this wrong once** and spent real time on it: a
triangle strip reading produced a convincing-looking car and was false. The game uses an
indexed triangle list, proven from the game's own loader. It's written up in `docs/C3D.md`
because the mistake is the interesting part.

**Index buffer**
A list of numbers saying "triangle one is made of vertices 4, 17 and 22". Lets vertices be
shared between triangles instead of repeated.

**Backface**
The back side of a triangle, which is normally invisible. If you can see through parts of a
model, a backface is usually why.

**Alpha test / alpha blend**
Two ways of handling transparency. An **alpha test** is all-or-nothing: a pixel is drawn or
it isn't. An **alpha blend** mixes the pixel with whatever's behind it. Glass and foliage
usually want blending; wire fences and cut-out decals usually want testing. Getting the pair
wrong makes things look subtly, annoyingly incorrect.

**State / state tracker**
The graphics settings that apply to the next thing you draw — which texture, whether
transparency is on, how it blends. Direct3D 7 lets a program set them one at a time. Modern
APIs make you declare the whole set at once and hand back a reusable token. A **state
tracker** is the layer that catches the old-style calls, notices when the combination
changes, and looks up the right token. **This is the RHI's core job and the biggest single
piece of work left in the port.**

**RHI — Render Hardware Interface**
The layer that sits between the game's drawing commands and the actual graphics API, so the
game asks for "draw this textured triangle" and doesn't care whether the machine is running
Vulkan, Direct3D, or something else. Writing one is "the mountain" — it's why 22 draws
against 256 state calls is a bigger problem than the raw count suggests.

**Vulkan**
The modern, cross-platform graphics API this port targets. On a Steam Deck it's what games
actually run on.

**SDL3**
A library that handles the unglamorous parts of being a program: opening a window, reading
a controller, playing sound. Lets the project avoid writing that part twice.

**Swapchain**
The queue of images a game hands to the display. "A real swapchain" means actual frames
reached an actual screen, not just a test that passed.

**Screenshot / frame**
A single picture of what the renderer produced. In this repo, every frame in `screenshots/`
came out of the port — and `docs/FRAME-NOTE.md` states, line by line, what came from the
game's own code and what is this project's own drawing code. It matters: the current viewer
draws the game's *data* with *our* renderer. **The game itself still doesn't draw.**

**Mesh deformation**
Changing the actual shape of a 3D model rather than swapping a picture on it. **CMR2 does
real mesh deformation for car damage** — it moves the vertices of a body panel when you hit
something, without ever changing how many vertices there are. This is why damage is on the
roadmap as a slider: the mechanism is already there, with named tunable values.

**zig cc**
A C/C++ compiler that, unusually, is a single self-contained download with no dependencies.
That's what makes it usable on a Steam Deck where you can't install system packages. One
quirk: it defaults to a different standard library, so builds must specify
`-target x86_64-linux-gnu`.

**distrobox**
A way to run a full Linux distribution (say, Fedora) as a container inside another one,
without administrator rights. Used here as an alternative build environment when
`zig cc` isn't enough.

**Asynchronous multiplayer**
Multiplayer where players don't have to be online at the same moment. **Rally is naturally
suited to it**: cars race the clock, not each other, so you race your stage, your time gets
submitted, and the results settle later. It needs a shared championship file and times, not
the frame-perfect synchronisation that a shooter would.

---

## Still unfamiliar?

Open an issue saying which word confused you. This file gets better one real question at
a time, and you will not be the only person who didn't know.
