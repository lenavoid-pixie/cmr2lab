#!/usr/bin/env python3
"""preview.py -- look at a rendered frame without a picture viewer.

Two modes:

  preview.py FRAME.bmp [cols]
      luminance map, hue map, top colours, brightness stats.

  preview.py --diff BACKDROP.bmp FRAME.bmp [cols]
      stats and maps over the pixels the CAR covers, where "the car" is defined
      as "differs from a frame rendered without the car". A gradient backdrop
      makes the plain "lit pixel" count meaningless -- it counts the backdrop --
      so this is the honest way to measure coverage and shading.

Both modes read the 32-bit BMP the viewer writes.
"""
import struct
import sys
from collections import Counter


def load_bmp(path):
    d = open(path, 'rb').read()
    off = struct.unpack('<I', d[10:14])[0]
    w, h = struct.unpack('<ii', d[18:26])
    bpp = struct.unpack('<H', d[28:30])[0]
    assert bpp in (24, 32), bpp
    stride = ((w * bpp // 8) + 3) // 4 * 4
    step = bpp // 8
    px = []
    for y in range(h):
        base = off + (h - 1 - y) * stride
        row = d[base:base + w * step]
        px.append([(row[x * step + 2], row[x * step + 1], row[x * step]) for x in range(w)])
    return w, h, px


RAMP = " .:-=+*#%@"


def lum(c):
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]


def hue_letter(c):
    r, g, b = c
    mx, mn = max(c), min(c)
    if mx < 26:
        return ' '
    if mx - mn < 20:
        return '.' if lum(c) < 90 else ('o' if lum(c) < 190 else '#')
    if r >= g and r >= b:
        return 'R' if b < r * 0.75 else 'M'
    if g >= r and g >= b:
        return 'G' if b < g * 0.8 else 'C'
    return 'B' if r < b * 0.8 else 'C'


def maps(w, h, px, cols, cell_div=2, mask=None, title=''):
    step = max(1, w // cols)
    cell = step * cell_div
    print(f"# {title}  {w}x{h}  cell {step}x{cell}")
    print('# luminance')
    for y in range(0, h - cell + 1, cell):
        line = ''
        for x in range(0, w - step + 1, step):
            s, n, m = 0.0, 0, 0
            for dy in range(cell):
                for dx in range(step):
                    if mask and not mask[y + dy][x + dx]:
                        continue
                    s += lum(px[y + dy][x + dx]); n += 1; m += 1
            line += RAMP[min(9, int(s / n / 25.6))] if n else ('.' if mask else ' ')
        print(line)
    print('# hue')
    for y in range(0, h - cell + 1, cell * 2):
        line = ''
        for x in range(0, w - step + 1, step):
            cnt = Counter(hue_letter(px[y + dy][x + dx])
                          for dy in range(min(cell * 2, h - y))
                          for dx in range(step)
                          if not mask or mask[y + dy][x + dx])
            line += cnt.most_common(1)[0][0] if cnt else (' ' if not mask else '.')
        print(line)


def main():
    args = sys.argv[1:]
    if args and args[0] == '--diff':
        A = load_bmp(args[1])
        wk, hk, pk = load_bmp(args[2])
        rest = [a for a in args[3:] if not a.startswith('-')]
        cols = int(rest[0]) if rest else 100
        w, h, pd = A
        mask = [[False] * w for _ in range(h)]
        car = []
        xs, ys = [], []
        for y in range(h):
            for x in range(w):
                a, b = pd[y][x], pk[y][x]
                if abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2]) > 6:
                    mask[y][x] = True
                    car.append(b)
                    xs.append(x); ys.append(y)
        n = len(car)
        print(f"# car pixels (differ from the no-car frame): {n} of {w*h} "
              f"({100.0*n/(w*h):.1f}%)")
        if not n:
            return
        print(f"# car bbox x[{min(xs)},{max(xs)}] y[{min(ys)},{max(ys)}] "
              f"= {max(xs)-min(xs)+1} x {max(ys)-min(ys)+1} px")
        print(f"# car mean colour ({sum(c[0] for c in car)/n:.1f},"
              f"{sum(c[1] for c in car)/n:.1f},{sum(c[2] for c in car)/n:.1f})  "
              f"mean lum {sum(lum(c) for c in car)/n:.1f}  "
              f"brightest {max(max(c) for c in car)}")
        print(f"# car pixels by brightness: <32 {sum(1 for c in car if lum(c)<32)}  "
              f"32-96 {sum(1 for c in car if 32<=lum(c)<96)}  "
              f"96-176 {sum(1 for c in car if 96<=lum(c)<176)}  "
              f">=176 {sum(1 for c in car if lum(c)>=176)}")
        maps(w, h, pk, cols, mask=mask, title='CAR ONLY (masked), frame ' + args[2])
        cnt = Counter(c for c in car)
        print('# top colours ON THE CAR')
        for c, k in cnt.most_common(10):
            print(f"   rgb{c}  {k} px")
        return

    path = args[0]
    cols = int(args[1]) if len(args) > 1 else 100
    w, h, px = load_bmp(path)
    allp = [c for row in px for c in row]
    lit = sum(1 for c in allp if max(c) > 26)
    print(f"# {path}  {w}x{h}  lit {lit} px ({100.0*lit/(w*h):.1f}%)  "
          f"brightest {max(max(c) for c in allp)}  "
          f"mean lum {sum(lum(c) for c in allp)/len(allp):.1f}")
    maps(w, h, px, cols, title='frame ' + path)
    print('# top colours')
    for c, k in Counter(allp).most_common(10):
        print(f"   rgb{c}  {k} px")


main()
