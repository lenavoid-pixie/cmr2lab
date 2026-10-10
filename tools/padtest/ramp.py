#!/usr/bin/env python3
"""ramp.py -- trigger resolution, both paths, same injected values.

One synthetic pad (Xbox-360 identity, 0..255 triggers like the Deck's own node).
A ramp of ABS_Z values is driven through it while two readers watch the same
device:

  * the kernel node -- what the port's evdev backend reads
  * a live SDL3 gamepad -- what SDL_GetGamepadAxis(LEFT_TRIGGER) reports

and each reading is converted into the port's own axis units (0..0x10000) with
the formula in deck_dinput.cpp axis_scaled() at g_axis_range=0x10000:

    game = round(value * 65536 / full_travel)

so 0..255 becomes 0..65536 and SDL's 0..32767 becomes 0..65536. The ramp is slow
enough (400 ms per step) that both readers catch every step; the step count from
each is printed, so a dropped sample is visible rather than silently averaged.
"""
import os, re, struct, subprocess, sys, threading, time

HERE = os.path.dirname(os.path.abspath(__file__))
VPAD = os.path.join(HERE, "vpad")
INFO32 = os.path.join(HERE, "sdlinfo32")
ENV = dict(os.environ)
ENV.update({"XDG_RUNTIME_DIR": "/run/user/1000", "DISPLAY": ":0",
            "WAYLAND_DISPLAY": "wayland-0", "SDL_ASSERT": "abort",
            "SDLPIN": "Xbox 360"})
ENV_VPAD = dict(ENV)
ENV_VPAD.update({"VPAD_NAME": "Microsoft X-Box 360 pad", "VPAD_VID": "0x045e",
                 "VPAD_PID": "0x028e"})

# starts non-zero and ends at zero: the device comes up at zero, and a value
# that does not CHANGE produces no event in either reader, so a leading 0 would
# be one step in the table and no step in the measurements.
VALUES = [1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 128, 160, 192, 224, 240, 248, 252, 254, 255, 0]
DWELL = 0.4


def main():
    p = subprocess.Popen([VPAD, "/tmp/ramp.node"], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                         text=True, env=ENV_VPAD)
    node = p.stdout.readline().strip()
    time.sleep(1.0)

    fd = os.open(node, os.O_RDONLY | os.O_NONBLOCK)
    raw = []
    stop = threading.Event()

    def raw_reader():
        while not stop.is_set():
            try:
                d = os.read(fd, 24 * 32)
            except BlockingIOError:
                time.sleep(0.0005)
                continue
            except OSError:
                return
            for i in range(len(d) // 24):
                _, _, typ, code, v = struct.unpack_from("qqHHi", d, i * 24)
                if typ == 3 and code == 2 and (not raw or raw[-1] != v):
                    raw.append(v)

    def send(line):
        p.stdin.write(line + "\n")
        p.stdin.flush()

    th = threading.Thread(target=raw_reader, daemon=True)
    th.start()

    marks = []

    def drive():
        time.sleep(1.2)                      # let SDL finish initialising
        for v in VALUES:
            marks.append((v, time.monotonic()))
            send("abs ABS_Z %d" % v)
            time.sleep(DWELL)

    dth = threading.Thread(target=drive, daemon=True)
    dth.start()

    r = subprocess.run([INFO32, str(1.2 + len(VALUES) * DWELL + 0.5)],
                       capture_output=True, text=True, env=ENV)
    dth.join()
    stop.set()
    time.sleep(0.2)

    # Absolute CLOCK_MONOTONIC stamps, so each observation is paired with the
    # injection that preceded it instead of with a position in a list.
    samples = []
    for ln in r.stdout.splitlines():
        m = re.match(r"^T=([0-9.]+) t=[0-9.]+ axis\s+LEFT_TRIGGER\s+=\s+(-?\d+)", ln)
        if m:
            samples.append((float(m.group(1)), int(m.group(2))))
    sys.stderr.write("probe rc=%s lines=%d samples=%d marks=%d\n"
                     % (r.returncode, len(r.stdout.splitlines()), len(samples), len(marks)))
    sdl = []
    for v, t in marks:
        seen = None
        for st, sv in samples:
            if st <= t:
                continue
            if seen is None or (seen is not None):
                pass
        # the value observed in the window (t, t+DWELL)
        win = [sv for st, sv in samples if t - 0.05 <= st < t + DWELL - 0.05]
        sdl.append(win[-1] if win else None)


    send("abs ABS_Z 0")
    send("quit")
    try:
        p.wait(timeout=2)
    except Exception:
        p.kill()
    os.close(fd)

    print("%8s %8s %8s %12s %12s" % ("inject", "raw", "sdl", "game_evdev", "game_sdl"))
    for i, v in enumerate(VALUES):
        rv = raw[i] if i < len(raw) else None
        sv = sdl[i] if i < len(sdl) else None
        g_ev = None if rv is None else round(rv * 65536 / 255)
        g_sdl = None if sv is None else round(sv * 65536 / 32767)
        print("%8d %8s %8s %12s %12s" % (v, rv, sv, g_ev, g_sdl))

    print()
    print("injected steps: %d   kernel observations: %d   SDL observations: %d"
          % (len(VALUES), len(raw), len(sdl)))
    pairs = [(round(rv * 65536 / 255), round(sv * 65536 / 32767))
             for rv, sv in zip(raw, sdl)]
    if pairs:
        worst = max(abs(a - b) for a, b in pairs)
        print("largest disagreement, in the game's units: %d of 65536 (%.3f%%)"
              % (worst, worst * 100.0 / 65536))
        print("distinct values the kernel path can reach across 0..255: %d"
              % len({round(i * 65536 / 255) for i in range(256)}))
        print("distinct values SDL's 0..32767 offers across 0..255:     %d"
              % len({round(round(i * 32767 / 255) * 65536 / 32767) for i in range(256)}))


if __name__ == "__main__":
    main()
