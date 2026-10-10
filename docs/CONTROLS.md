# Controls

**This is the intended mapping. It is not implemented yet** — no pad input currently
reaches a car, because no stage has been loaded. It is written down because it is a
design decision, and design decisions that live in someone's head are the ones that get
built three different ways.

Decided by the project owner, 2026-10-10.

---

## Driving

| Input | Action |
|---|---|
| **Right trigger (R2)** | Throttle — analogue, progressive |
| **Left trigger (L2)** | Brake — analogue, progressive |
| **B** | Handbrake |
| **Y** | Change camera |
| **A / X** | Manual gear change — *optional; only active if the player selects a manual gearbox* |
| Left stick | Steering — analogue |
| D-pad | Menus and any legacy steer/throttle mapping |

**Analogue or nothing.** This is a rally game, and a rally game with on/off throttle is
not a rally game. Both triggers must report their full range, not a pressed/not-pressed
flag. That requirement is the reason there is real work here rather than a keybinding
table — see below.

## Menus

**Menus are free to be sensible.** The owner's instruction: keep the existing behaviour
or do whatever works best. Menu navigation does not have to mirror the original game's
keyboard layout, and a mapping chosen for playability is preferred over one chosen for
fidelity.

That is a deliberate exception to the rule that the port stays faithful to the original.
It applies to menus only, not to driving.

## Why driving is harder than it looks

**The game is from 2000, and its input model assumes a combined pedal axis.**

In that era, a joystick with a combined throttle/brake axis was the norm: one axis, centre
at rest, positive for throttle and negative for brake. The game ships expecting that. A
Steam Deck has **two separate analogue triggers**, which is a better design and not one
the game anticipated. Its throttle and brake both default to reading the same axis.

So the port has to synthesise the axis the game expects from the two inputs it actually
gets. The intended form is:

```
axis1 = R2 − L2
```

Full right trigger gives full throttle; full left trigger gives full brake; releasing both
returns to centre. That is a faithful reproduction of hardware the game was written for,
built out of hardware it wasn't.

**The trap, stated plainly:** naively binding both triggers to the same axis makes them
fight — pressing one while the other is held produces a sum that means nothing. The
combination has to happen in the port layer, deliberately, in one place.

## What is still undecided

- **Which of A / X shifts up and which shifts down.** This should follow the game's own
  manual-gearbox defaults rather than being invented, and those have not been read yet.
- **Whether manual gearbox also needs a clutch or neutral.** Rally games of this era
  generally do not, but this has not been verified against the original.

Both are open because they can be answered from the game's code, and guessing at them
would put a wrong answer in a document people trust.

> **Both were answered, and the answers are still good.** The section below went out of this
> page when it was restructured on 2026-10-10 19:57; the reading behind it did not stop being
> true. It is recoverable in full from commit `5651627` (`git show 5651627:docs/CONTROLS.md`).
> In short, read out of the decompilation and the shipped string table:
>
> - **Gear Up is `]` and Gear Down is `[`** on the game's own default keyboard (`Input.cpp:696`,
>   DIK_RBRACKET `0x1B` / DIK_LBRACKET `0x1A`; the action names come from the shipped English
>   string table at ids `0x6b`–`0x74`). A and X are Deck buttons and no 2000 default names
>   them, so there is no original to be faithful to — the recommendation is **A = up, X = down**,
>   because in both of the game's own layouts the up-shift is the first and higher of the pair.
> - **There is no clutch.** The word appears once in the whole decompilation and not as an
>   input. **Neutral is a gear value, not a button**: `0` is neutral, `1`–`6` forward, `7`
>   reverse (`Car.h:214`), and two buttons cover the whole box including reverse.
> - The manual gearbox is per-car (`Car.field_0xb48`: `1` the driver shifts, `2` the game
>   does) — so "optional, only active if the player selects a manual gearbox" is right, and
>   the code says which way that switch is set.
>
> Flagged rather than silently restored: the restructure was a separate call and this page is
> its author's. If the citations are wanted back inline, that is a one-commit revert of a
> section that already exists.

---

## Two tiers, on purpose

Controller feel is not a nicety in a rally game. On a handheld, with a precise car on a
loose surface, it is most of the experience. So it gets treated as a requirement.

**EZ.** One choice, and it is correct. No dead zone to find, no curve to reason about — pick
it and drive. Someone who will never open a tuning screen must still get the best defaults
we can produce, and those defaults have to be *chosen*, not fallen into.

**Advanced.** Dead zone, response curve, steering linearity, saturation, per-trigger dead
zones, vibration. Everything tunable, for the people who want to tune.

**The rule that keeps them from fighting each other:** depth is available, never compulsory.
The advanced layer must never sit between someone and driving. No tuning screen is on the
path to the start line.

## Also adjustable

Dead zone, and anything else that turns out to matter once the car is actually moving.
The list above is a starting point, not a ceiling — the owner's position is that more
tunability is better, provided it stays out of the way.


---

## Where this actually lives: the tuning seam

**Why the tiers above are an input-layer decision and not a UI task.** The car adds no shaping
of its own. `StageObjects.cpp:12707-12718` reads the steering axis and scales it linearly —
`FixMulShift32(half, 0x3f0000)` — with no dead zone, no curve, no saturation and no threshold;
the throttle and brake path a few lines below it (`12728-12746`) is the same shape. So every
dead zone, every response curve and every saturation the car will ever feel is applied on the
way out of the input layer, **or it does not exist anywhere in the program.** There is no other
seam to bolt a curve onto later, which is exactly why it was worth knowing before a stage
exists rather than after.

The one shaping the *game* does is a dead zone of its own: `0xc8`, 2 % of full scale, set on
every axis it finds at `Input.cpp:966` through `DIPROP_DEADZONE`. DirectInput's contract is
that the **device** applies it, and in this port the device is `deck_dinput.cpp`. So the
game's dead zone arrives as a number and the input layer is what honours it.

**The seam is two functions wide, and the tiers now sit on it.**

| function | what it is |
|---|---|
| `axis_scaled(idx, trigger)` | every analogue value the game ever sees: dead zone, response curve, saturation, range, in that order |
| `deck_pedal_axis()` | the game's single combined pedal axis — literally `axis_scaled(L2) − axis_scaled(R2)` |

The pedal being a subtraction of two shaped axes is why the per-trigger dead zones reach the
pedal for free, instead of needing a second code path that would then drift from the first.
Nothing else in the file writes a value the game reads back as an axis.

**Status, 2026-10-10: the seam is built and measured; the GUI is not, and was not the task.**

* `DECK_PAD_TIER=ez` is the default and is **measured bit-identical** to the port as it
  shipped, over an 18-point steering sweep against a reference build of the same source.
* It **stays identical with every advanced knob set to something wild**, because in EZ the code
  returns before it reads a single knob — the check sits behind the tier test, not behind a
  "did they set one" test. A player who never opens a tuning screen cannot be affected by one
  existing. That is *"I don't want to make it too confusing"* enforced in code.
* `DECK_PAD_TIER=advanced` exposes dead zone, curve and saturation per axis, the two triggers
  separately. Measured: a 30 % steering dead zone, a 30 % left-trigger dead zone (knee at 77 of
  255), full lock at half travel from a saturation of 5000, and — the per-trigger claim — the
  **right** trigger's dead-zone knob leaves the **left** trigger's sweep byte-identical.
* **What EZ contains is the game's own numbers** — its `0xc8`, saturation 10000, a linear curve.
  EZ has to be a decision and not a second behaviour, and the only dead zone with evidence
  behind it is the one the game asks for. **There is no stage to drive yet, so there is no
  measurement that could justify a different one, and inventing one would be taste dressed up
  as engineering.** `pad_tune_ez()` is the single place that answer changes when there is
  something to change it with.
* **Vibration is deliberately not in this.** It is not an axis; it is the `IDirectInputEffect`
  path (`Input.cpp:1795`, `Input.cpp:1953`, `GUID_ConstantForce`), and the device answers
  `CreateEffect()` with `DIERR_UNSUPPORTED` on purpose, rather than handing back an effect
  object that silently does nothing.

**One wrinkle found and left standing on purpose:** the trigger path has three thresholds, not
one — the game's 2 % dead zone (the analogue value), `DECK_PAD_PEDAL_PCT` at 4 % (whether a
trigger counts as touched at all) and `DECK_PAD_TRIGGER_PCT` at 25 % (when it becomes the
digital Up/Down key). Collapsing them into one number would change behaviour in EZ, so they are
made visible and nameable instead. A tuned per-trigger dead zone only ever *widens* the pedal
gate, so EZ keeps the 4 % floor it always had.

Two limits, unchanged: **no thumb has ever pressed the Deck's physical stick**, and **no stage
has been driven** — the numbers above are measured against the input layer, not against a car.
Knobs, numbers and reconstructible evidence: `work/PADTUNE/`, `tools/padtest/tune/`.
