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

## The things on the list

Each one gets an honest split: what's actually hard, and what only looks hard.

### Controller compatibility

Playable on an Xbox pad, a DualSense, whatever's plugged in — not just Deck controls.

**Not hard, if it's built right the first time.** The input layer goes through SDL's gamepad
abstraction, which already normalises every pad on the market, rather than being written
against the names of the specific buttons on one device. Do that and other controllers work
by construction. Write it narrowly instead and adding them later is a rewrite.

**This one is not "later".** The input layer is being written now, which makes it an
architectural decision rather than a feature request.

**Status, 2026-10-10: the foundation exists, the feature does not.** The input layer now reads
pads through SDL's gamepad API, so an Xbox pad, a DualSense, a Switch Pro pad and the Deck's
own controls all arrive in one vocabulary by construction. What a player would notice —
driving with those buttons — is still not wired, and the Deck's physical controls have never
been pressed by a test, so that part is verified structurally rather than by a thumb.

**And controller *feel* now has a seam to live in.** The tiers described in `docs/CONTROLS.md`
are implemented in that same input layer: EZ is the default, and it is measured bit-identical
to the port as it shipped — including with every advanced knob set to something wild, because
in EZ the code returns before it reads one. The depth is real and it is behind the default,
which is what makes the "one GUI" below possible rather than a wall of numbers. Details and
numbers: `docs/CONTROLS.md`, `work/PADTUNE/`.

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

### Better spectators

The crowds are flat sprites — a cut-out photograph standing next to the scenery, next to a
cut-out tree. The owner's gripe, and a fair one: he wants a crowd that reacts, moves along
the track, waves, takes pictures. The reference points he named are the crowds in WRC 4 and
Richard Burns Rally.

**Three separate jobs wearing one word.** Improving the crowd *art* is a data job — the
container format is cracked and repacking is verified byte-identical, so better textures,
more variety and more animation frames are export, edit, repack. Teaching the crowd to
*behave* — react to the car passing, jump back, raise a camera — is a genuine new subsystem:
the engine has to know how close the car is to each spectator and pick a frame accordingly.
Building the crowd out of actual *geometry* is the third and largest job, because it means
replacing the billboard draw path rather than feeding it.

**The framing that matters:** the goal is not "make everything 3D". Billboards are not a
2000-era compromise that we grew out of — impostors are still what modern engines use for
anything the camera is not close to. The right target is level of detail: real geometry for
what is near the car, impostors for what is not. Polygons spent where the eye actually is.

**This is the same answer for trees** — no trunks and 2D foliage, which the owner named as
his one real gripe with the original. The trees were flat because a forest at 150 km/h was
unaffordable in 2000, not because anyone thought a photograph of a tree was better. Near
trees as geometry, distant forest as impostor.

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

## The one that is not a feature: one GUI, and it is the shape of the project

From the same conversation as the crowds, and it is the largest thing said in it:

> *"re-master it... and make it public... from one GUI where you can choose the little bits
> and pieces."*

That is not a fourth item on a list. It is the **port, the modkit, the feature toggles and the
installer converging into one product**, and it settles a question that has been implicit since
the first version of this page: **the GUI installer is the shape of this project, not a wrapper
on top of it.** Every feature here — the controller tiers, the camera tab, the gauges, the
crowds, the trees — ends up as a control in that one place, and each one is only as shippable
as its defaults are good.

Which is why the two-tier split matters beyond controller feel. If depth is compulsory, that
GUI is a wall of numbers and the port is a simulator somebody has to configure before they can
drive. If the default is correct and the depth sits behind it, the same GUI serves a person who
wants to race and a person who wants to adjust — and the owner gets both of his sentences at
once, the one at the top of this page and the one about not making it too confusing.

**Not started, and deliberately not designed here.** What exists is the seam a GUI's numbers
would land in, and the contract it has to honour: one choice that is correct, and depth behind
it.

---

## What is not on this list, on purpose

- **Anything that changes how the car drives by default.** Adjustability means you *can*.
- **A rewritten game.** The engine stays the engine. Everything above is a layer over it.
- **A feature list with dates.** Unknown things stay unknown until they are measured.
