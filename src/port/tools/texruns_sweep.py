#!/usr/bin/env python3
"""texruns_sweep.py -- run the viewer's texture-run path over every car in the install.

The per-triangle texture path (patch_textures.py) is the fix that made the car a
car, and until this ran it had only been exercised on 205a1N. This says, per car:

  parts, verts, tris, kept%, runs, distinct textures, runs refused, exit status

and prints the cars where anything is off -- a texture index out of range, a part
that produced no run, a car that draws nothing, a nonzero exit. "It worked on the
one car I looked at" is not a result; this is.

usage: texruns_sweep.py [CARSDIR] [JOBS]
"""
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

BIN = os.path.expanduser('~/lena/cmr2deck/build/cmr2deck')
GAME = os.path.expanduser('~/lena/.lena_cmr2')
CARS = sys.argv[1] if len(sys.argv) > 1 else os.path.join(GAME, 'game/Game/Cars')
JOBS = int(sys.argv[2]) if len(sys.argv) > 2 else 4

ENV = dict(os.environ,
           XDG_RUNTIME_DIR='/run/user/1000', WAYLAND_DISPLAY='wayland-0',
           OFFW='64', OFFH='64')


def one(path):
    car = os.path.basename(path)[:-4]
    try:
        p = subprocess.run([BIN, car, '--game', GAME, '--shot', f'/tmp/sweep_{car}.bmp'],
                           env=ENV, capture_output=True, text=True, timeout=180)
    except subprocess.TimeoutExpired:
        return (car, 124, None)
    out = p.stdout
    def g(pat, cast=str, default=None):
        m = re.search(pat, out)
        return cast(m.group(1)) if m else default
    rec = {
        'parts':  g(r'parts=(\d+)', int),
        'verts':  g(r'verts=(\d+)', int),
        'tris':   g(r'tris=(\d+)', int),
        'kept':   g(r'face records kept = ([\d.]+)%', float),
        'runs':   g(r'(\d+) runs from', int),
        'tex':    g(r'(\d+) distinct textures', int),
        'refused': g(r'\((\d+) runs refused', int),
        'draws':  g(r'(\d+) texture-run draws', int),
        'holes':  g(r'holes/empty parts=(\d+)', int),
        'bridge': g(r'bridging = ([\d.]+)%', float),
    }
    return (car, p.returncode, rec)


paths = sorted(p for p in
               (os.path.join(CARS, f) for f in os.listdir(CARS) if f.endswith('.c3d')))
print(f"# {len(paths)} cars from {CARS}, {JOBS} at a time", flush=True)
rows, bad = [], []
with ThreadPoolExecutor(max_workers=JOBS) as ex:
    for car, rc, rec in ex.map(one, paths):
        if rc != 0 or not rec or rec['runs'] is None:
            bad.append((car, f"exit {rc} / no [TEXRUNS] line"))
            continue
        rows.append((car, rec))
        if rec['refused']:
            bad.append((car, f"{rec['refused']} runs refused: no texture"))
        if rec['holes']:
            bad.append((car, f"{rec['holes']} empty part(s)"))
        if rec['tris'] == 0:
            bad.append((car, "0 triangles"))

print(f"# cars completed: {len(rows)}   with something to look at: {len(bad)}")
tot_t = sum(r[1]['tris'] for r in rows)
tot_r = sum(r[1]['runs'] for r in rows)
print(f"# triangles across the corpus: {tot_t}   runs: {tot_r}")
nz = [r for r in rows if r[1]['tris']]
print(f"# runs per car: min {min(r[1]['runs'] for r in nz)} "
      f"median {sorted(r[1]['runs'] for r in nz)[len(nz)//2]} "
      f"max {max(r[1]['runs'] for r in rows)}")
print(f"# distinct textures per car: min {min(r[1]['tex'] for r in nz)} "
      f"max {max(r[1]['tex'] for r in rows)}")
print(f"# kept% (triangles / face records): min {min(r[1]['kept'] for r in nz):.1f} "
      f"max {max(r[1]['kept'] for r in nz):.1f}")
print(f"# worst bridging: {max(r[1]['bridge'] for r in rows):.2f}%")
print('# --- cars with something to look at ---')
for car, why in bad:
    print(f"   {car:12s} {why}")
open('/tmp/texruns_sweep.tsv', 'w').write(
    'car\tparts\tverts\ttris\tkept_pct\truns\ttextures\trefused\tdraws\tholes\tbridge_pct\n' +
    '\n'.join(f"{c}\t{r['parts']}\t{r['verts']}\t{r['tris']}\t{r['kept']}\t{r['runs']}\t"
              f"{r['tex']}\t{r['refused']}\t{r['draws']}\t{r['holes']}\t{r['bridge']}"
              for c, r in rows) + '\n')
print('# full table: /tmp/texruns_sweep.tsv')
