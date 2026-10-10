# Modes we want to exist

Written down because they were asked for, not because they are scheduled. Nothing here
is promised and nothing here is priced from memory. The difficulty notes are honest
guesses about *shape* — how many new systems a thing needs — not estimates of time.

The original game decides which car you drive in which event, which stages make up a
championship, and whether you race alone. Every item below is the same request wearing
different clothes: **let the player write the order instead.**

## 1. Car choice unlocked across events

The special events should be able to use any car in the game, rather than the categories
the original locks them into.

**Shape: cheap.** This is a constraint enforced in the game's own logic, not an engine
feature. No new subsystem. The risk is small and the payoff is immediate — it is the
difference between a menu and a garage.

## 2. Special stages as a custom option — and a custom championship

Pick your own stages, your own order, your own car, and run it.

**Shape: medium, and importantly it is all *above* the simulation.** A menu, a config
format, persistence, and some routing. It does not touch physics, rendering, or the
D3D7 layer at all. That makes it a candidate for doing early and independently of the
graphics work — it needs the game *running*, not the game *drawing*.

## 3. Rallycross — head-to-head, mixed surface, joker laps

The one he got genuinely excited about, and the reason is a good one: this is what rally
itself grew into when people wanted to actually **watch** it. Rallycross exists because
rally cars racing each other on a closed circuit is a better spectacle than one car
against a clock. A joker lap is the modern version — one longer alternate route you must
take exactly once per race, so the lead can change without anyone being faster.

**Shape: the big one.** The original is point-to-point against the clock. Racing needs
opponent AI that can actually drive a circuit, collision between cars, junction and
routing logic, and joker-lap enforcement. That is a new subsystem, not a setting.

It is also the item on this page that would make the result a *remaster* rather than a
re-release. A port gets you the 2000 game on hardware that no longer runs it. A rallycross
mode gets you a game that never existed.

## 4. Cross-game content — bring your own

Cars and stages from other titles in the series, if the player already owns them.

**Shape: a separate project per source game, not a switch.** The other titles are
different engines with different file formats. A converter can be published; the assets
inside it cannot. That is not a limitation we are working around — it is the reason any
of this is publishable at all. Code travels. Ripped content does not.

**Not in scope for the port.** This is its own thing, filed here so the idea is not lost.
