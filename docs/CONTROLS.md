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
