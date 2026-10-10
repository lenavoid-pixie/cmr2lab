#!/usr/bin/env python3
"""vtest.py -- drive a synthetic pad and measure what the port's input layer does with it.

  vtest.py bench <sdl|evdev> [trials]   latency of one reader, same injector
  vtest.py idents                       one pad per identity: what SDL calls it
  vtest.py game  [seconds]              the real game, pinned at the synthetic pad

Everything is timestamped against CLOCK_MONOTONIC (time.monotonic()), which is
system-wide, so the moment of injection and the moment of observation are on the
same clock whatever process they happen in.
"""
import os, subprocess, sys, time, signal, re

HERE = os.path.dirname(os.path.abspath(__file__))
VPAD = os.path.join(HERE, "vpad")
GAME = os.path.join(HERE, "out", "cmr2")
GAMEDIR = "/home/deck/lena/.lena_cmr2/game"
INFO32 = os.path.join(HERE, "sdlinfo32")
BENCH32 = os.path.join(HERE, "padbench32")
NODE_FILE = "/tmp/vtest.node"

ENV = dict(os.environ)
ENV.update({
    "XDG_RUNTIME_DIR": "/run/user/1000",
    "DISPLAY": ":0",
    "WAYLAND_DISPLAY": "wayland-0",
    "SDL_ASSERT": "abort",
})

IDENT_XBOX   = ("Microsoft X-Box 360 pad", "0x045e", "0x028e")
IDENT_DS5    = ("DualSense Wireless Controller", "0x054c", "0x0ce6")
IDENT_SWITCH = ("Pro Controller", "0x057e", "0x2009")


class VPad:
    def __init__(self, ident=IDENT_XBOX):
        self.ident = ident
        env = dict(ENV)
        env["VPAD_NAME"], env["VPAD_VID"], env["VPAD_PID"] = ident
        self.p = subprocess.Popen([VPAD, NODE_FILE], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  text=True, env=env)
        self.node = self.p.stdout.readline().strip()
        if not self.node:
            raise SystemExit("vpad did not report a node")

    def cmd(self, line):
        """Send one action; return the monotonic time the line was written."""
        t = time.monotonic()
        self.p.stdin.write(line + "\n")
        self.p.stdin.flush()
        return t

    def close(self):
        try:
            self.p.stdin.write("quit\n")
            self.p.stdin.flush()
        except Exception:
            pass
        try:
            self.p.wait(timeout=2)
        except Exception:
            self.p.kill()


def bench(kind, trials=20):
    """Inject the same step N times and read the reader's own log for the delay."""
    v = VPad()
    time.sleep(1.0)
    if kind == "sdl":
        reader = subprocess.Popen([BENCH32, "sdl", BENCH_NAME, "90"],
                                  stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                  text=True, env=ENV)
    else:
        reader = subprocess.Popen([BENCH32, "evdev", v.node, "90"],
                                  stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                  text=True, env=ENV)

    # drain the reader's banner lines
    time.sleep(0.5)
    os.set_blocking(reader.stdout.fileno(), False)
    reader.stdout.read()

    out = []
    for i in range(trials):
        t = v.cmd("abs ABS_Z %d" % (60 + (i % 5) * 20))
        out.append((t, i))
        time.sleep(0.12)
        v.cmd("abs ABS_Z 0")
        time.sleep(0.12)

    time.sleep(0.3)
    os.set_blocking(reader.stdout.fileno(), True)
    reader.send_signal(signal.SIGTERM)
    try:
        lines = reader.stdout.read()
    except Exception:
        lines = ""
    reader.wait(timeout=3)

    # match each observation to the nearest preceding injection
    obs = []
    for ln in lines.splitlines():
        m = re.match(r"^(\w+) ([0-9.]+) (?:axis\d+|abs\d+) (-?\d+)", ln)
        if m:
            obs.append((float(m.group(2)), int(m.group(3)), ln))
    inj = sorted(t for t, _ in out)
    deltas = []
    for t_obs, val, ln in obs:
        if val == 0:
            continue
        prev = [t for t in inj if t <= t_obs]
        if not prev:
            continue
        deltas.append((t_obs - max(prev)) * 1000.0)
    v.close()
    if not deltas:
        print("no observations matched -- raw reader output follows")
        print(lines[:2000])
        return
    deltas.sort()
    print("%s: n=%d  min %.2f ms  median %.2f ms  max %.2f ms"
          % (kind, len(deltas), deltas[0], deltas[len(deltas) // 2], deltas[-1]))


def idents():
    """For each identity: what does SDL see, what does it call it, and what are
    the NAMED buttons that come out of it."""
    for ident in (IDENT_XBOX, IDENT_DS5, IDENT_SWITCH):
        v = VPad(ident)
        time.sleep(1.2)
        p = subprocess.run([INFO32, "0.2"], capture_output=True, text=True, env=ENV)
        keep = [l for l in p.stdout.splitlines()
                if "name=" in l or "mapping=" in l or "vendor=" in l]
        print("=== injected: name=%r vid=%s pid=%s -> node %s" % (ident[0], ident[1], ident[2], v.node))
        for l in keep:
            print("   ", l.strip()[:180])
        # and the named buttons a press produces
        v.cmd("key BTN_SOUTH 1"); time.sleep(0.3)
        p2 = subprocess.run([INFO32, "0.3"], capture_output=True, text=True, env=ENV)
        for l in p2.stdout.splitlines():
            if "button" in l:
                print("   press BTN_SOUTH ->", l.strip())
        v.close()
        time.sleep(0.8)


def game(secs=40, pin="Xbox 360"):
    v = VPad(IDENT_XBOX)
    time.sleep(0.8)
    env = dict(ENV)
    env.update({
        "DECK_DIN_TRACE": "3",
        "DECK_PAD_KB_TRACE": "1",
        "DECK_PAD_SDL_NAME": pin,
        "DECK_PAD_TRIGGER_PCT": "25",
    })
    out = open("/tmp/vtest-game.log", "w")
    p = subprocess.Popen([GAME], cwd=GAMEDIR, stdout=out, stderr=subprocess.STDOUT,
                         env=env, stdin=subprocess.DEVNULL)
    time.sleep(6)
    script = [
        ("abs ABS_Z 200", 1.2), ("abs ABS_Z 0", 1.2),
        ("abs ABS_RZ 255", 1.2), ("abs ABS_RZ 0", 1.2),
        ("abs ABS_X 32767", 0.8), ("abs ABS_X 0", 0.8),
        ("key BTN_SOUTH 1", 0.6), ("key BTN_SOUTH 0", 0.6),
        ("key BTN_TL 1", 0.6), ("key BTN_TL 0", 0.6),
        ("key BTN_DPAD_UP 1", 0.6), ("key BTN_DPAD_UP 0", 0.6),
    ]
    for line, wait in script:
        v.cmd(line)
        time.sleep(wait)
    time.sleep(1.0)
    p.terminate()
    try:
        p.wait(timeout=5)
    except Exception:
        p.kill()
    out.close()
    v.close()

    log = open("/tmp/vtest-game.log", errors="replace").read().splitlines()
    print("=== the port's own trace, pad and key path ===")
    for l in log:
        if "pad(sdl)" in l or "pad(kernel)" in l or "joy axes" in l or "PADKB" in l:
            print("   ", l[:200])


BENCH_NAME = None

if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "game"
    if mode == "bench":
        BENCH_NAME = sys.argv[3] if len(sys.argv) > 3 else IDENT_XBOX[0]
        bench(sys.argv[2] if len(sys.argv) > 2 else "sdl",
              int(sys.argv[4]) if len(sys.argv) > 4 else 20)
    elif mode == "idents":
        idents()
    else:
        game(int(sys.argv[2]) if len(sys.argv) > 2 else 40)
