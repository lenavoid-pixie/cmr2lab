# padtest — the instruments behind the pad claims

The port reads pads through SDL's gamepad API (`SDL_OpenGamepad`,
`SDL_GetGamepadAxis`, `SDL_GetGamepadButton`), so an Xbox pad, a DualSense, a
Switch Pro pad and the Steam Deck's own controls all arrive in one vocabulary —
named axes, named buttons — instead of the Deck's control names being the shape
everything else is bent around. The kernel evdev reader is kept underneath as a
fallback that fills the *same* named state.

The claims that go with that are tested with these five files rather than
asserted. Nothing here touches the game's data; `vpad` creates a uinput gamepad
whose identity is a parameter, so "does SDL call an Xbox pad the same thing as a
DualSense" is asked of a real device node.

## Build

Needs `zig` (on the Deck: `/home/deck/lena/toolchain/zig/zig`) and the port's
32-bit SDL3 at `<port>/third_party/sdl3-i386` (built by
`third_party/sdl3-i386/build-sdl3-i386.sh`).

```sh
# 32-bit readers, linked against the port's own SDL3 (static, no runtime deps)
zig cc   -target x86-linux-gnu.2.43 -w -O2 -I <port>/third_party/sdl3-i386/include \
         -o sdlinfo32 sdlinfo32.c <port>/third_party/sdl3-i386/lib/libSDL3.a \
         -lpthread -ldl -lm -L/usr/lib32
zig cc   -target x86-linux-gnu.2.43 -w -O2 -I <port>/third_party/sdl3-i386/include \
         -o padbench32 padbench32.c <port>/third_party/sdl3-i386/lib/libSDL3.a \
         -lpthread -ldl -lm -L/usr/lib32
# the injector is a host tool
zig cc -target x86_64-linux-gnu -w -O2 -o vpad vpad.c
```

## What each one answers

| file | question |
|---|---|
| `vpad.c` | a uinput gamepad whose VID/PID/name are parameters — the synthetic thumb. Everything from `/dev/uinput` inward is the kernel's own input stack. |
| `sdlinfo32.c` | what SDL sees: every joystick, whether it is a *gamepad*, SDL's own type for it, its evdev/hidraw path, and the mapping string. `SDLPIN=<substring>` picks which one to watch. |
| `padbench32.c` | latency of one reader (SDL gamepad vs the kernel node) on the same injected step, both timestamped `CLOCK_MONOTONIC`. |
| `ramp.py` | trigger resolution: the same ramp of `ABS_Z` read through both paths and converted into the port's own axis units. |
| `vtest.py` | three pad identities end-to-end, and the real game driven with a synthetic pad pinned by name (`PADTEST_GAME` overrides the binary path). |

## Results on the Steam Deck (2026-10-10)

```
# SDL sees the Deck's own pad, exactly as the system's SDL3 does
[0] name='Steam Deck' is_gamepad=1 path=/dev/hidraw2
[0] name='Steam Deck Controller' type=Steam path=/dev/hidraw2

# three injected identities, one vocabulary
045e:028e -> 'Xbox 360 Controller'                type=Xbox360
054c:0ce6 -> 'PS5 Controller'                     type=PS5
057e:2009 -> 'Nintendo Switch Pro Controller'     type=SwitchPro

# latency, same injected step, 20 trials each
sdl  : min 0.05 ms  median 0.09 ms  max 0.11 ms
evdev: min 0.05 ms  median 0.06 ms  max 0.11 ms

# trigger resolution across the kernel's 0..255
largest disagreement between the two paths: 2 of 65536 (0.003%)
distinct values reachable: 256 (kernel) / 256 (SDL)
```

Two limits, stated because they are real: no process can press the *physical*
Deck controls, so the Deck's own buttons are verified structurally (SDL opens
that pad and maps it; the same path the synthetic pads take is the one feeding
it) and not by a press; and a 32-bit SDL3 built **without** libudev sees zero
joysticks on this machine, which is why the port's copy is built with it — see
`third_party/sdl3-i386/README.md`.

## `tune/` — the tuning seam and the two controller tiers

The instruments behind `docs/CONTROLS.md` § *The tuning seam*, so those numbers can be
re-made rather than believed. Same idea as the rest of this directory: nothing re-implements
the shaping it is measuring — the readout is the port's own `DECK_DIN_TRACE` trace.

| file | what it is |
|---|---|
| `patch_padtune.py` | the one edit to `platform/deck_dinput.cpp`. Anchored on exact strings, and it **refuses to apply** if the source has drifted from the md5 the INPUTGEN lane published. `--check` reports without changing anything |
| `sweep.py` | injects an axis sweep into a synthetic pad, **waits for the trace to go quiet** before recording, and reads back what the input layer hands the game |
| `run_sweeps.sh` | runs A–J in the order they are quoted in the docs (~5 min) |
| `*.txt` / `*.raw` | the recorded tables and the raw traces |

Reading them, in order:

* `old-ez` is the **reference** — the pre-seam source built with the same trace level, so the
  instrument is identical and only the behaviour can differ — against `new-ez` and
  `new-ez-knobs`. The three are **identical, 18 of 18**, including with every advanced knob
  set to something wild. That is the EZ claim.
* `adv-dz3000`, `adv-curve180`, `adv-curve30`, `adv-sat5000` are the knobs doing what they say.
* `trig-ez` / `trig-l-dz3000` / `trig-r-dz3000` are the per-trigger claim: same sweep, same
  binary, one knob moved and the swept axis responds, the other knob moved and it does not.

Two things the harness had to get right, both learned the hard way and both worth keeping:
the game polls the joystick on **its own schedule**, so a fixed settle reads a value one
injection stale and that stale-vs-fresh difference is pure timing, not a result; and the
default trace threshold (0.4 % of full scale) **cannot resolve a dead zone edge**, because a
value coming off zero is by definition small. `DECK_DIN_TRACE=4` fixes the second, waiting
for quiet fixes the first.

The reference binary (`out-ref/cmr2`) is not published — it is 33 MB and it is one command to
rebuild: revert `deck_dinput.cpp` to `deck_dinput.cpp.bak-pre-padtune`, apply only the
`DECK_DIN_TRACE=4` hunk, and link.
