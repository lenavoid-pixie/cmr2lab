#!/usr/bin/env python3
"""tex_verify.py -- does the C viewer's texture decode match an independent one?

Two independent decoders written from the container/BC3 specs, one in Python
(here) one in C (cmr2deck). If they agree byte for byte, the C decode is not
"plausible", it is right.

  decode A: the viewer's own path      -> via TEXDUMP=DIR (PPM per texture)
  decode B: this file, from the raw .bfl blocks, reading the container with the
            published CMPR/BFL spec and BC3 / TGA straight off the spec

usage:
  python3 tools/tex_verify.py --sweep            # every car: parse + counts only
  python3 tools/tex_verify.py --exact CAR [CAR]  # byte-exact, named cars
"""
import gzip, os, struct, subprocess, sys, tempfile, shutil

GAME = os.environ.get("CMR2_GAME", "/home/deck/lena/.lena_cmr2")
CARS = os.path.join(GAME, "game/Game/Cars")
VIEWER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "cmr2deck")


# ---------------------------------------------------------------- container --
def bfl_blocks(path):
    """-> payload, [(name, offset, size)] using the CMPR spec (see bfl-read.py)."""
    full = open(path, "rb").read()
    if full[:2] == b"\x1f\x8b":
        full = gzip.decompress(full)
    pay = full[8:]                                   # CMPR + containerSize
    toc = struct.unpack("<I", pay[-4:])[0]           # last 4 bytes of the payload
    pos, out = toc, []
    while pos < len(pay) - 4:
        size, off, slen = struct.unpack("<III", pay[pos:pos + 12]); pos += 12
        slenp = slen + ((4 - slen % 4) % 4)
        name = pay[pos:pos + slen].rstrip(b"\0").decode("ascii", "replace"); pos += slenp
        out.append((name, off, size))
    return pay, out


# ------------------------------------------------------------------ decoders --
def c565(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)


def bc3(d, W, H):
    bw, bh = (W + 3) // 4, (H + 3) // 4
    out = bytearray(W * H * 3)
    for by in range(bh):
        for bx in range(bw):
            o = (by * bw + bx) * 16
            blk = d[o:o + 16]
            a0, a1 = blk[0], blk[1]
            abits = int.from_bytes(blk[2:8], "little")
            al = [a0, a1]
            if a0 > a1:
                al += [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
            else:
                al += [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
            c0, c1 = struct.unpack("<HH", blk[8:12])
            bits = int.from_bytes(blk[12:16], "little")
            cols = [c565(c0), c565(c1)]
            if c0 > c1:
                cols.append(tuple((2 * cols[0][i] + cols[1][i]) // 3 for i in range(3)))
                cols.append(tuple((cols[0][i] + 2 * cols[1][i]) // 3 for i in range(3)))
            else:
                cols.append(tuple((cols[0][i] + cols[1][i]) // 2 for i in range(3)))
                cols.append((0, 0, 0))
            for y in range(4):
                for x in range(4):
                    py, px = by * 4 + y, bx * 4 + x
                    if py >= H or px >= W:
                        continue
                    c = cols[(bits >> (2 * (y * 4 + x))) & 3]
                    o2 = (py * W + px) * 3
                    out[o2], out[o2 + 1], out[o2 + 2] = c
    return W, H, bytes(out)


def tga(d, ):
    idlen, cmap, typ = d[0], d[1], d[2]
    if cmap or typ not in (2, 10):
        return None
    W, H = struct.unpack("<HH", d[12:16])
    bpp = d[16] // 8
    if d[16] not in (24, 32):
        return None
    p = 18 + idlen
    total = W * H
    top = bool(d[17] & 0x20)
    out = bytearray(total * 3)
    k = 0
    if typ == 2:
        for k in range(total):
            px = d[p + k * bpp:p + k * bpp + bpp]
            y, x = divmod(k, W)
            if not top:
                y = H - 1 - y
            o = (y * W + x) * 3
            out[o], out[o + 1], out[o + 2] = px[2], px[1], px[0]
    else:
        while k < total:
            h = d[p]; p += 1
            run = (h & 0x7f) + 1
            if h & 0x80:
                px = d[p:p + bpp]; p += bpp
                for _ in range(run):
                    if k >= total:
                        break
                    y, x = divmod(k, W)
                    if not top:
                        y = H - 1 - y
                    o = (y * W + x) * 3
                    out[o], out[o + 1], out[o + 2] = px[2], px[1], px[0]
                    k += 1
            else:
                for _ in range(run):
                    if k >= total:
                        break
                    px = d[p:p + bpp]; p += bpp
                    y, x = divmod(k, W)
                    if not top:
                        y = H - 1 - y
                    o = (y * W + x) * 3
                    out[o], out[o + 1], out[o + 2] = px[2], px[1], px[0]
                    k += 1
    return W, H, bytes(out)


def decode_block(blob):
    """container magic decides, exactly like tex_decode() does"""
    if blob[:4] == b"DDS ":
        if blob[84:88] != b"DXT5":
            return None
        W = struct.unpack("<I", blob[16:20])[0]
        H = struct.unpack("<I", blob[12:16])[0]
        return bc3(blob[128:], W, H)
    if len(blob) >= 18 and blob[1] == 0 and blob[2] in (2, 10) and blob[16] in (24, 32):
        return tga(blob)
    return None


def read_ppm(f):
    d = open(f, "rb").read()
    p = d.split(b"\n", 3)
    w, h = map(int, p[1].split())
    return w, h, p[3]


# ---------------------------------------------------------------------- runs --
def car_names():
    return sorted(f[:-4] for f in os.listdir(CARS) if f.endswith(".c3d"))


def run_viewer(car, dumpdir):
    env = dict(os.environ, TEXDUMP=dumpdir)
    r = subprocess.run([VIEWER, car, "--game", GAME, "--shot", "/tmp/tex_verify.bmp"],
                       capture_output=True, text=True, env=env, timeout=300)
    return r


def sweep():
    cars = car_names()
    ok = bad = notextures = 0
    rows = []
    for c in cars:
        d = tempfile.mkdtemp(prefix="texv-")
        try:
            r = run_viewer(c, d)
            txt = r.stdout
            ndec = nph = None
            for line in txt.splitlines():
                if line.startswith("[OK] textures:"):
                    import re
                    m = re.search(r"textures: (\d+) decoded.*?(\d+) placeholders", line)
                    if m:
                        ndec, nph = int(m.group(1)), int(m.group(2))
            ncmp = len([f for f in os.listdir(d) if f.endswith(".ppm")])
            stray = len([f for f in os.listdir(d) if not f.endswith(".ppm")])
            if r.returncode != 0 or ndec is None:
                bad += 1; rows.append((c, "RUNFAIL rc=%d nfiles=%d stray=%d" % (r.returncode, ncmp, stray)))
            elif ndec == 0:
                notextures += 1; rows.append((c, "no textures decoded"))
            else:
                ok += 1
        finally:
            shutil.rmtree(d, ignore_errors=True)
    print("cars=%d  decoded>0: %d  decoded==0: %d  run failed: %d" % (len(cars), ok, notextures, bad))
    for c, why in rows:
        print("   ", c, why)
    return bad


def exact(cars):
    total = mismatch = 0
    for c in cars:
        # same resolution the viewer uses: literal, then minus the trailing
        # variant letter, then +N  (205a1N -> 205a1.bfl, and there is no
        # 205a1N.bfl at all)
        path = None
        for cand in (c, c[:-1] if len(c) > 4 else "", c + "N"):
            f = os.path.join(CARS, cand + ".bfl") if cand else ""
            if f and os.path.exists(f):
                path = f; break
        if not path:
            print(c, "no .bfl"); continue
        pay, ents = bfl_blocks(path)
        d = tempfile.mkdtemp(prefix="texv-")
        try:
            r = run_viewer(c, d)
            if r.returncode != 0:
                print(c, "RUN FAILED"); continue
            dumps = {}
            for f in os.listdir(d):
                if f.endswith(".ppm"):
                    dumps[f[3:].rsplit(".", 1)[0].lower()] = f
            kind = {}
            for name, off, size in ents:
                base = name.rsplit(".", 1)[0]
                if base.lower() not in dumps:
                    print(c, "  no dump for", name); continue
                w1, h1, p1 = read_ppm(os.path.join(d, dumps[base.lower()]))
                ref = decode_block(pay[off:off + size])
                if ref is None:
                    print(c, "  ", name, "no reference decoder"); continue
                w2, h2, p2 = ref
                n = min(len(p1), len(p2)) // 3
                diff = sum(1 for k in range(n) if p1[k * 3:k * 3 + 3] != p2[k * 3:k * 3 + 3])
                total += 1
                k = name.rsplit(".", 1)[1]
                kind[k] = kind.get(k, 0) + 1
                if (w1, h1) != (w2, h2) or diff:
                    mismatch += 1
                    print(c, "  MISMATCH", name, "%dx%d vs %dx%d" % (w1, h1, w2, h2), "diff", diff, "/", n)
            print("%-8s %d blocks, %s  -> %s" % (c, len(ents), kind, "IDENTICAL" if not mismatch else "MISMATCH"))
        finally:
            shutil.rmtree(d, ignore_errors=True)
    print("blocks compared: %d   mismatches: %d" % (total, mismatch))
    return mismatch


if __name__ == "__main__":
    a = sys.argv[1:]
    if not a or a[0] == "--sweep":
        sys.exit(1 if sweep() else 0)
    if a[0] == "--exact":
        sys.exit(1 if exact(a[1:]) else 0)
    print(__doc__)
