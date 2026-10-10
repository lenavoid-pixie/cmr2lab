#!/usr/bin/env python3
"""flank_art.py -- is the car body texture's own artwork the same on the two
halves a v-mirrored UV pair lands on?

Context (progress.md §0a): a car's mirrored flank vertex carries the SAME u and
the COMPLEMENT of v (v' = 1 - v).  Standing at the two flanks therefore samples
two horizontal bands of the body texture at the same u, one of them with v
running backwards.  *If the two bands are vertical mirrors of each other as
images*, both flanks show the same artwork upright, and the only difference left
between them is the horizontal sense -- which is exactly the mirrored lettering
that was reported.  If they are not, the left flank is showing different art.

This script takes the texture the viewer already decoded and prints all four
correlations, so the claim is a measurement and not a description.

  input   a P6 PPM, as dumped by the viewer:  TEXDUMP=DIR cmr2deck <car> ...
          (`cmr2deck` prints each decoded texture as `NN_<name>.ppm`)

  usage   tools/flank_art.py DUMPDIR [TEXTURE_PPM] [U0 U1 V0 V1 V0B V1B]

     e.g. TEXDUMP=/tmp/q2tex build/cmr2deck seaa1N --game ~/.lena_cmr2
          tools/flank_art.py /tmp/q2tex 02_ASCDBoDf.ppm 0.40 0.76 0.79 0.88 0.12 0.21

  defaults are the seaa1N body texture and the bands progress.md §0a cites.

stdlib only.  Prints raw-byte NCC over the band at the same u columns; the two
bands must be the same pixel size (they are, when the v-bands are complements on
a 1024-wide texture: 0.79-0.12 = 0.88-0.21 etc.).
"""
import math, os, sys


def read_ppm(path):
    d = open(path, "rb").read()
    tok = d.split(b"\n", 3)
    if tok[0].strip() != b"P6":
        raise SystemExit(f"not a P6 PPM: {tok[0][:20]!r}")
    w, h = (int(x) for x in tok[1].split())
    px = tok[3]
    if len(px) < w * h * 3:
        raise SystemExit(f"short pixel block: {len(px)} < {w*h*3}")
    return w, h, px


def band(px, W, H, u0, u1, v0, v1):
    x0, x1 = int(u0 * W), int(u1 * W)
    y0, y1 = int(v0 * H), int(v1 * H)
    return [px[(y * W + x0) * 3:(y * W + x1) * 3] for y in range(y0, y1)]


def flipud(rows):
    return rows[::-1]


def fliplr(rows):
    out = []
    for row in rows:
        n = len(row) // 3
        nb = bytearray(len(row))
        for i in range(n):
            nb[i * 3:i * 3 + 3] = row[(n - 1 - i) * 3:(n - 1 - i) * 3 + 3]
        out.append(bytes(nb))
    return out


def ncc(a, b):
    va = [x for r in a for x in r]
    vb = [x for r in b for x in r]
    n = len(va)
    ma, mb = sum(va) / n, sum(vb) / n
    num = sum((va[i] - ma) * (vb[i] - mb) for i in range(n))
    da = math.sqrt(sum((x - ma) ** 2 for x in va))
    db = math.sqrt(sum((x - mb) ** 2 for x in vb))
    return num / (da * db)


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    dump = argv[0]
    name = argv[1] if len(argv) > 1 else "02_ASCDBoDf.ppm"
    nums = [float(x) for x in argv[2:8]] or [0.40, 0.76, 0.79, 0.88, 0.12, 0.21]
    u0, u1, v0, v1, w0, w1 = nums[:6]
    path = name if os.path.isabs(name) else os.path.join(dump, name)
    W, H, px = read_ppm(path)
    A = band(px, W, H, u0, u1, v0, v1)
    B = band(px, W, H, u0, u1, w0, w1)
    print(f"# {path}  {W}x{H}")
    print(f"# A = u[{u0},{u1}] v[{v0},{v1}]   B = u[{u0},{u1}] v[{w0},{w1}]"
          f"   ({len(A)}x{len(A[0])//3} px each)")
    if (len(A), len(A[0])) != (len(B), len(B[0])):
        print("  !! bands are not the same pixel size -- the numbers below are not"
              " comparable as pixels; fix the bands or the texture size")
    for label, b in (("B (no flip)", B), ("flipud(B)", flipud(B)),
                     ("fliplr(B)", fliplr(B)), ("fliplr(flipud(B))", fliplr(flipud(B)))):
        print(f"  NCC(A, {label:18s}) = {ncc(A, b):+.3f}")
    print("  -> a high flipud value is the claim: the two bands are the same artwork,")
    print("     one of them upside down.  A negative fliplr value is the other half of")
    print("     it: they are NOT horizontal mirrors of each other.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
