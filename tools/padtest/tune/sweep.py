#!/usr/bin/env python3
"""sweep.py -- inject an axis sweep into a synthetic pad and read back what the
port's input layer hands the game.

  sweep.py --bin PATH --tag NAME [--axis ABS_X] [--vals 0,600,...] [--env K=V]...

The readout is the port's OWN trace (DECK_DIN_TRACE=4, which prints on any
change), read after every single injection. Nothing here re-implements the
shaping it is measuring -- if the measurement and the code disagreed, the
measurement would be the thing that was wrong, and that is the point.

That every injection gets a reading matters: a value that produces NO new trace
line means the output did not move, which is the whole answer for a dead zone
and would be invisible if the numbers were zipped against the lines afterwards.
"""
import argparse, os, re, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
VPAD = os.path.join(HERE, "vpad")
if not os.path.exists(VPAD):
    VPAD = "/home/deck/lena/work/INPUTGEN/vpad"
GAMEDIR = "/home/deck/lena/.lena_cmr2/game"
LOG = "/tmp/sweep.log"

BASE_ENV = {
    "XDG_RUNTIME_DIR": "/run/user/1000",
    "DISPLAY": ":0",
    "WAYLAND_DISPLAY": "wayland-0",
    "SDL_ASSERT": "abort",
}

TRACED = {"ABS_X": "lX", "ABS_RX": "lRx", "ABS_RY": "lRy",
          "ABS_Y": "lY(pedal)", "ABS_Z": "lZ(L2)", "ABS_RZ": "lRz(R2)"}
FIELDS = ["lX", "lY(pedal)", "lZ(L2)", "lRx", "lRy", "lRz(R2)"]

JOY = re.compile(r"joy axes lX=(-?\d+) lY\(pedal\)=(-?\d+) lZ\(L2\)=(-?\d+) "
                 r"lRx=(-?\d+) lRy=(-?\d+) lRz\(R2\)=(-?\d+)")


def count_lines():
    try:
        return len(JOY.findall(open(LOG, errors="replace").read()))
    except Exception:
        return 0


def latest(text, pos):
    last = None
    for line in text.splitlines():
        m = JOY.search(line)
        if m:
            last = int(m.group(pos + 1))
    return last


def run(binary, axis, vals, extra_env, wait_attach=7.0, settle=0.45,
        max_wait=3.0, quiet=0.6):
    env = dict(os.environ)
    env.update(BASE_ENV)
    env.update({"DECK_DIN_TRACE": "4", "DECK_PAD_SDL_NAME": "Xbox 360"})
    env.update(extra_env)

    padenv = dict(os.environ); padenv.update(BASE_ENV)
    padenv.update({"VPAD_NAME": "Microsoft X-Box 360 pad",
                   "VPAD_VID": "0x045e", "VPAD_PID": "0x028e"})
    v = subprocess.Popen([VPAD, "/tmp/sweep.node"], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                         text=True, env=padenv)
    v.stdout.readline().strip()
    time.sleep(1.0)

    if os.path.exists(LOG):
        os.unlink(LOG)
    f = open(LOG, "w")
    g = subprocess.Popen([binary], cwd=GAMEDIR, stdout=f, stderr=subprocess.STDOUT,
                         env=env, stdin=subprocess.DEVNULL)
    try:
        time.sleep(wait_attach)
        text = open(LOG, errors="replace").read()
        if "pad(sdl)" not in text:
            print("WARNING: no 'pad(sdl)' line after %.1fs -- the game may not "
                  "have opened the pad" % wait_attach)

        pos = FIELDS.index(TRACED[axis])
        rows = []
        for value in vals:
            v.stdin.write("abs %s %d\n" % (axis, value)); v.stdin.flush()
            # WAIT FOR QUIET, do not sleep a fixed time. The game polls the
            # joystick on its own schedule, so a fixed settle reads a value
            # that is sometimes one injection stale -- and a stale reading
            # compared against a fresh one is a difference that is pure
            # timing, which is exactly the kind of "result" this lane must not
            # report. Record only once the trace has stopped moving.
            t0 = time.time(); last_change = t0
            seen = count_lines()
            while time.time() - t0 < max_wait:
                time.sleep(0.15)
                c = count_lines()
                if c != seen:
                    seen = c; last_change = time.time()
                elif time.time() - last_change >= quiet:
                    break
            got = latest(open(LOG, errors="replace").read(), pos)
            rows.append((value, got))
        v.stdin.write("abs %s 0\n" % axis); v.stdin.flush()
        time.sleep(settle)
    finally:
        g.terminate()
        try:
            g.wait(timeout=5)
        except Exception:
            g.kill()
        f.close()
        try:
            v.stdin.write("quit\n"); v.stdin.flush(); v.wait(timeout=2)
        except Exception:
            v.kill()
    return open(LOG, errors="replace").read(), rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True)
    ap.add_argument("--tag", required=True)
    ap.add_argument("--axis", default="ABS_X")
    ap.add_argument("--vals", default="0,600,700,900,1000,2000,4000,6000,8000,"
                                     "10000,12000,16000,20000,24000,28000,32767")
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--raw", default="")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--settle", type=float, default=0.45)
    ap.add_argument("--max-wait", type=float, default=3.0)
    ap.add_argument("--quiet-gap", type=float, default=0.6)
    args = ap.parse_args()

    vals = [int(x) for x in args.vals.split(",") if x != ""]
    extra = {}
    for kv in args.env:
        k, _, val = kv.partition("=")
        extra[k] = val

    if not os.path.exists(args.bin):
        sys.exit("no binary at %s" % args.bin)

    text, rows = run(args.bin, args.axis, vals, extra, max_wait=args.max_wait,
        quiet=args.quiet_gap)
    if args.raw:
        open(args.raw, "w").write(text)

    print("# %s   axis=%s   %s" % (args.tag, args.axis,
          " ".join("%s=%s" % (k, v2) for k, v2 in sorted(extra.items())) or "(no knobs)"))
    print("injected\tgame value\t% full")
    for value, got in rows:
        gv = "none" if got is None else str(got)
        pct = "" if got is None else "%.1f%%" % (100.0 * got / 65536.0)
        print("%d\t%s\t%s" % (value, gv, pct))
    if not args.quiet:
        hits = sum(1 for _, x in rows if x is not None)
        print("# %d of %d injections produced a reading" % (hits, len(rows)))


if __name__ == "__main__":
    main()
