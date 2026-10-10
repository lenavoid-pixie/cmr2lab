# Where this is going

**None of this is built.** This page exists because the direction of a project is a design
decision, and a direction that lives in one person's head gets built four different ways.
Treat it as intent, not a roadmap. There are no dates here and there will not be.

The owner's words, on why this page exists at all:

> *"I just want such adjustability for my game, so much adjustability, because I want to
> adjust it, you know, that's the point."*

That is the design principle for everything below. A rally game you cannot tune is a rally
game somebody else configured for you.

---

## The direction, in one line

**A port that runs the original faithfully, and lets you change the parts you want to change.**

Fidelity is the default. Everything adjustable is opt-in, off by default, and never changes
how the original drives unless you ask it to.

---

## The four things on the list

Each one gets an honest split: what's actually hard, and what only looks hard.

### Controller compatibility

Playable on an Xbox pad, a DualSense, whatever's plugged in — not just Deck controls.

**Not hard, if it's built right the first time.** The input layer goes through SDL's gamepad
abstraction, which already normalises every pad on the market, rather than being written
against the names of the specific buttons on one device. Do that and other controllers work
by construction. Write it narrowly instead and adding them later is a rewrite.

**This one is not "later".** The input layer is being written now, which makes it an
architectural decision rather than a feature request.

### Wheel support

**Axes and pedals: probably straightforward.** SDL exposes wheels, and the game already
expects a DirectInput joystick, so there is a natural place to plug one in.

**Force feedback: the real work.** The game's FFB is built on DirectInput effect objects, and
mapping those onto SDL's haptics is genuine engineering, not configuration.

Untested. This is the one item here where the honest answer is *we don't know yet* — and it
stays that way until someone plugs a wheel in and finds out.

### A camera tab

Per-camera adjustment: field of view, position, head height, widen the cockpit view.

**Feasible.** This is mostly *overriding values the engine already computes* rather than
inventing a camera system — the view matrix exists, we'd be changing what goes into it. The
work is finding every place the engine writes those values, which is a search problem, not a
research problem.

Worth noting: there is already a free-camera project for replays. Same machinery, different
consumer. One of them probably builds the other.

### Different gauges

Swapping the car's dashboard and HUD art, including gauges from other games in the series.

**This sounds like the hardest one and is probably the easiest** — because the prerequisite
is already done. The game's asset container format is cracked and repacking is verified
byte-identical against the originals, which means replacing a gauge is export, edit, repack,
with a proven safety net underneath. The tool for browsing and exporting that art is already
a project of its own.

**One honest caveat:** art ripped out of another commercial game is not something anyone can
redistribute. Using it on a copy you own is your business; putting it in a release is not.
That line gets stated plainly rather than hand-waved.

---

## What is not on this list, on purpose

- **Anything that changes how the car drives by default.** Adjustability means you *can*.
- **A rewritten game.** The engine stays the engine. Everything above is a layer over it.
- **A feature list with dates.** Unknown things stay unknown until they are measured.
