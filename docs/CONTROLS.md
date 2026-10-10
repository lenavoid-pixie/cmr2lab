# Controls

**Status, stated precisely** — the driving mapping below is the owner's decision
(2026-10-10). Everything under *Answered from the game's own code* below was read out of
the decompilation and the shipped game data on the same day, with file and line numbers, so
it can be re-checked instead of believed.

What exists in the port **today**, as opposed to what is decided:

* **The pad's device state is read through SDL's gamepad API** (2026-10-10) —
  `SDL_OpenGamepad` / `SDL_GetGamepadAxis` / `SDL_GetGamepadButton`, so the layer
  works in SDL's vocabulary (named axes, named buttons) instead of the Deck's
  control names; an Xbox pad, a DualSense, a Switch Pro pad and the Deck's own
  controls all arrive in that one vocabulary, and the Deck is one device in the
  list rather than the shape everything is bent around. The kernel evdev reader
  is kept underneath as a fallback that fills the *same* named state, so the
  path this port shipped on is still there. Everything the game sees is
  unchanged in shape: ±0x10000 axes, the combined pedal axis, and the same DIK
  keyboard mirror. Measured rather than asserted: three injected pad identities
  all come back as the same named buttons through SDL's own mapping database,
  the Deck's controller is opened over `/dev/hidraw2` exactly as the system's
  own SDL3 opens it, and the same injected trigger values through both backends
  land within one rounding step of each other (2 of 65536; 256 of 256 trigger
  values either way; read latency indistinguishable at 0.05–0.11 ms). What is
  **not** verified: a thumb on the physical Deck controls, which no process can
  press. Instruments: `tools/padtest/`.
* **The combined pedal axis is implemented and measured** — one function
  (`deck_dinput.cpp`, `deck_pedal_axis()`), handed to the game on joystick axis 1. See
  *The combined pedal axis* — including the part of this page that was **wrong until it was
  measured**.
* **The driving button layer is not implemented as such** — there is no pad layout written
  for a stage yet — but the bits do not start from zero, because the pad already speaks the
  game's keyboard and the car reads that state through the game's own bit map. Y already
  lands on Change Camera and the gear pair already works; on L1/R1, not on A/X. See *What the
  pad already does on a stage*. What the owner's mapping implies is mostly a **move**, not a
  build.
* **One thing in that layer was wrong and is fixed** (2026-10-10): the two triggers' keyboard
  mirror was inverted — R2 set the Brake key and L2 set the Accelerate key. Fixed and
  rebuilt; see the same section for the evidence, which is a reading of two code paths, not a
  stage.

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

## The combined pedal axis

**The game is from 2000, and its input model assumes a combined pedal axis.**

In that era, a joystick with a combined throttle/brake axis was the norm: one axis, centre
at rest, one direction for throttle and the other for brake. The game ships expecting that.
A Steam Deck has **two separate analogue triggers**, which is a better design and not one
the game anticipated. Its throttle and brake both default to reading the same axis — the
game's own default joystick mapping reads car control 0 (steer) from axis 0 and controls 2
(throttle) and 3 (brake) from **the same** axis 1 (`GetEnabledControllerAxisBinding`, read
live out of the running game: 0, 1, 1).

So the port has to synthesise the axis the game expects from the two inputs it actually
gets. The intended form is one combined axis built in the port layer, and that is what the
port does:

```
axis1 = L2 − R2      (R2 = throttle, L2 = brake)
```

Full right trigger gives full throttle; full left trigger gives full brake; releasing both
returns to centre; the left stick still owns the axis when neither trigger is touched. That
is a faithful reproduction of hardware the game was written for, built out of hardware it
wasn't.

**CORRECTION (2026-10-10, after measuring).** This section previously said `axis1 = R2 − L2`
with "positive for throttle". That is the wrong sign for this device and would have driven
the car with the pedals swapped. The owner's mapping is unchanged — R2 is throttle, L2 is
brake — but **R2 pulls the axis negative**. The reason is in the game, not in our taste:

* The consumer of the analogue path is `StageObjects.cpp:12723`. When throttle and brake are
  the same axis it branches on the **sign** of the axis value and on a per-controller
  polarity flag, `CInput::GetControllerField120(device)`: with the flag at `0`, `raw <= 0`
  is throttle and `raw > 0` is brake. On the pad this port presents, that flag reads `0`
  (measured live, not assumed).
* That flag is a **game option with a name**: text `0x20b` in the shipped English string
  table reads **"Flip Accelerate/Brake Axis"**, sitting in the game's own analogue setup
  screen next to the saturation/deadzone hints. So the polarity is the game's switch, and a
  player who flips it will swap our pedals — which is the original's behaviour, not a bug
  we get to remove.
* A second, independent check agrees: `ReadJoystick` (`Input.cpp:1608`) sets the Accelerate
  bit when the joystick's Y is negative and the Brake bit when it is positive. "Away from
  you is throttle" is the game's own default, and the port follows it.

**The trap, stated plainly:** naively binding both triggers to the same axis makes them
fight — pressing one while the other is held produces a sum that means nothing. The
combination has to happen in the port layer, deliberately, in one place. It does.

---

## Answered from the game's own code

### Gear Up and Gear Down — which action is which, and which key

The game has **nine** car bindings, in a fixed order, and the order is the action list. The
names come from the shipped English string table (`CountrySpecific/Europe/englishtext.bfl`,
gzip — `zcat` it and count lines: the line number *is* the text id) at ids `0x6b`–`0x74`:
Left, Right, Accelerate, Brake, Handbrake, **Gear Up, Gear Down**, Change Camera, Rear View,
Pause. The keyboard default for each binding is set in `Input.cpp:696` (arrows keyboard) and
`Input.cpp:706` (numpad keyboard, for a second player on the same keyboard):

| # | action | default key | as a bit | numpad keyboard |
|---|---|---|---|---|
| 0 | Left | ← | `0x01` | numpad 4 |
| 1 | Right | → | `0x02` | numpad 6 |
| 2 | Accelerate | ↑ | `0x04` | numpad 8 |
| 3 | Brake | ↓ | `0x08` | numpad 2 |
| 4 | Handbrake | Space | `0x10` | numpad 3 |
| 5 | **Gear Up** | **`]`** (DIK_RBRACKET `0x1B`) | `0x20` | **Page Up** |
| 6 | **Gear Down** | **`[`** (DIK_LBRACKET `0x1A`) | `0x40` | **Page Down** |
| 7 | Change Camera | C | `0x80` | numpad 9 |
| 8 | Rear View | R | `0x100` | numpad 7 |

The chain from key to gearbox, in three hops, all in the decompilation:

1. `CInput::ReadKeyboardDevice` (`Input.cpp:1527`) turns those keys into bits
   `0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x100`.
2. `g_carButtonMasks` (`StageObjects.cpp:8082`) maps mask index → bit, so index 5 is Gear Up
   and index 6 is Gear Down.
3. The car's per-frame input (`StageObjects.cpp:12776`) reads **rising edges only** and asks
   the gearbox: mask 5 → `field_0x1d4 = 1`, mask 6 → `0xff`. `AutoGear_RequestAdjacentGear`
   (`TrackCollision.cpp:303`) takes `1` as *gear + 1* and `0xff` as *gear − 1*.

So on the game's own defaults: **`]` is the up-shift and `[` is the down-shift**, Page Up /
Page Down on the numpad keyboard. The port's pad layer already used that pair for L1/R1 for
exactly this reason.

Edge-triggered means a **tap** shifts one gear; holding the button does not repeat
(`field_0x8` is computed as `(new ^ old) & new` in `Input.cpp:1702`).

**A and X: the code cannot answer this, and this page will not pretend it does.** A and X
are Steam Deck buttons; no 2000 default names them, so there is no original binding to be
faithful to. What the original does say is the *order* of the pair: in both of its default
layouts the up-shift is the first and right-hand/higher key of the two. Applying that to the
Deck — where A is the primary button and the right-hand one of the pair — the recommendation
is **A = Gear Up, X = Gear Down**. It is a recommendation with a reason, not a code fact, and
it remains the owner's choice.

### Clutch and neutral — no clutch exists, and there is no neutral control

**No clutch input exists anywhere in the game.** The complete set of car actions is the nine
above plus Pause; there is no clutch binding, no clutch key, and no clutch bit in the input
state. The word "clutch" appears exactly once in the whole decompilation, in a comment
describing a sound-parameter array — not an input, not a control.

**Neutral is a gear value, not a button.** The gearbox has eight values: `0` is neutral, `1`–`6`
are forward, `7` is reverse (`Car.h:214`; the dashboard prints them as the string
`"RN123456"`, `HudDash.cpp:338`). Neutral is where the box *sits* rather than something the
driver presses:

* while a shift is in progress the current gear reads `0` for a per-car number of frames —
  the shift takes time, and the game animates the neutral itself (`TrackCollision.cpp:1346`);
* shifting down from 1st lands on `0`; shifting down from `0` lands on `7` (reverse);
  shifting up from reverse lands back on `0`. Two buttons cover the whole box, including
  reverse, with no third control.

### "If wanted" — the automatic gearbox really does ignore the buttons

Which of the two gearbox models a car has is `Car.field_0xb48`: `1` = the driver shifts,
`2` = the game shifts (`Car.cpp:7225`). The value comes from the driver/category record flag,
and the frontend prints that same flag as `MT`/`AT` (`FrontendScreens.cpp:5594`, and
`FrontendScreens.cpp:11972` picks between them). In `TrackCollision.cpp:1355`, a driver shift
request is only honoured in mode `1`; mode `2` runs the game's own selector and the buttons
do nothing for the car.

Shift requests are also ignored while the transmission is not live (`0xb9c == 0`): the game
clears it while the car waits at the start line and after it has finished, and sets it while
running. One caveat, said out loud rather than buried: the decompilation's own header comment
labels `0xb9c` "automatic gearbox enabled", which contradicts how `StageObjects.cpp:12776`
uses it. The comment is the unverified part; the use is what the code does.

## What the pad already does on a stage (read from two code paths, not measured)

There is no stage in the port yet, so nothing here was measured on one. It is the crossing of
two things that *were* read: the pad layer's current layout (`deck_dinput.cpp`,
`pad_keys_build()`) and the car's own bit meanings (`g_carButtonMasks`, above). The pad
presents itself to the game as the keyboard — its keys are ORed into the same DIK state the
game polls — so while **driving** this is what the pad's buttons mean to the car:

| Deck input | key the pad sends | car bit | what the car does |
|---|---|---|---|
| A | Enter | — | nothing: Enter is in no car binding |
| B | Esc | `0x400` | Pause (the tenth action), not a car action |
| X | Space | `0x10` | **Handbrake** |
| Y | C | `0x80` | **Change Camera** — already what the owner wants |
| L1 | `[` | `0x40` | **Gear Down** |
| R1 | `]` | `0x20` | **Gear Up** |
| Select / Start | Esc / Enter | — | Pause / nothing |
| D-pad, left stick | arrows | `1,2,4,8` | Steering, Accelerate, Brake |
| L2 past 25% | Down | `0x8` | Brake (digital mirror of the analogue pedal) |
| R2 past 25% | Up | `0x4` | Accelerate (same) |

So the owner's mapping is a move: **gears from L1/R1 to A/X** (A = Gear Up, X = Gear Down),
**handbrake from X to B**, Y stays where it is, L1/R1 freed. The triggers keep their analogue
job on axis 1 and their coarse keyboard mirror on top of it.

**Fixed while checking this (2026-10-10).** That mirror was inverted: the code had
`trigger_pct(4)` — L2, the brake — driving the `up`/Accelerate key and `trigger_pct(5)` — R2,
the throttle — driving the `down`/Brake key, which contradicted its own comment, the analogue
axis (where R2 pulls axis 1 negative and negative is throttle) and the pad's own layout.
`deck_dinput.cpp` swapped them; the platform layer rebuilds and links clean (`link32.sh`,
5/5 objects, link exit 0). **Evidence level, stated plainly: a reading plus a compile, not a
stage.** Pulling R2 on a stage is still the measurement that would replace the argument, and
it cannot be made until a stage loads.

**One caveat that is a real limit:** the game swaps to a hardcoded arrow/Return/Escape set
while input is paused (`CGameInfo::SetInputAndGamePaused` → `Input.cpp:1523`), which is how
its own frontend keeps working whatever a player has bound. So what the pad's keys mean
depends on the game's state as well as on ours — which is exactly the menu/driving split the
next section describes.

## What this means for the pad layer (a design note, not a measurement)

The frontend is driven by **window messages** (`WM_KEYDOWN` → `CInput::QueueVirtualKeyPress`),
while the car is driven from the **DirectInput device state** read every frame. Those are two
different paths, so the pad layer can hold two layouts at once without them fighting: the
menu layout the pad already uses (A = confirm, B = back, D-pad = arrows), and a driving
layout (R2/L2 on axis 1 as above, A = Gear Up, X = Gear Down, B = handbrake, Y = camera,
left stick = steering). Menus stay free to be sensible; driving stays faithful.
