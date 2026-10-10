#!/usr/bin/env python3
"""flank_mirror.py -- is the mirrored lettering on one car flank faithful to the
game's data, or a bug in our viewer's texture binding?

Three measurements, all from the files, no rendering:

  1. PAIR      find a mesh triangle and its mirror image (x, y, -z) in the same
               car and compare the two triangles' texture coordinates.  If the
               mirrored vertices carry the SAME u and the complement of v
               (v' = 1 - v), the livery's "along the car" direction runs the
               same way on both flanks -- which means one flank shows the
               artwork the other way round from outside.  If instead u is
               complemented (u' = 1 - u), the model was UV'd to read correctly
               on both sides.
  2. TEXTURES  do the left flank and the right flank name the same texture?
               A distinct LEFT texture would make the mirroring our bug.
  3. CORPUS    (1) and (2) over every .c3d in the install, as counts.
  4. BODIES    (1) and (2) over the 23 *a1N exterior car files only -- the set
               that carries a livery.  (The *a5 / *c5 files are cockpit
               interiors; the flank-band triangles in them are door cards,
               dash and glass, not artwork.)

Usage:
    tools/flank_mirror.py PAIR  <car>              # one car, the pair relation
    tools/flank_mirror.py PAIR  --all              # the pair relation, all 259 cars
    tools/flank_mirror.py CORPUS [CARS_DIR]        # every .c3d, as counts
    tools/flank_mirror.py BODIES [CARS_DIR]        # the 23 exterior bodies only
    tools/flank_mirror.py PAIRS <car> [--xyz]      # every matched pair, one line each
"""
import gzip, math, os, struct, sys
from collections import Counter

DEFAULT_CARS = os.path.expanduser("~/lena/.lena_cmr2/game/Game/Cars")


# ---------------------------------------------------------------- container --
def payload(path):
    """gzip -> CMPR -> PP_F, the same unwrap cmr2deck.c does."""
    b = open(path, "rb").read()
    if b[:2] == b"\x1f\x8b":
        b = gzip.decompress(b)
    if b[:4] == b"CMPR":
        return b[8:8 + struct.unpack("<I", b[4:8])[0]]
    return b


def parse(path):
    """-> (triangles, texture names).  Layout = docs/C3D.md + Sector_RelocateStageMeshFile."""
    P = payload(path)
    if P[:4] != b"PP_F":
        raise ValueError("not PP_F")
    u32 = lambda o: struct.unpack("<I", P[o:o + 4])[0]
    s32 = lambda o: struct.unpack("<i", P[o:o + 4])[0]
    u16 = lambda o: struct.unpack("<H", P[o:o + 2])[0]
    c10, c14, c20 = u32(0x10), u32(0x14), u32(0x20)
    c18, c1a, c1c = u16(0x18), u16(0x1a), u16(0x1c)
    c1e, c28, c26, c24 = u16(0x1e), u16(0x28), u16(0x26), u16(0x24)
    C1A = 48 + c18 * 396
    C20 = C1A + (c1a + c1c) * 288 + c1c * 160 + c1e * 136 + c28 * 28
    VBLK = C20 + c20 * 76
    texbase = u32(0x0c)
    tex = [P[texbase + i * 260:texbase + i * 260 + 260].split(b"\0")[0]
           .decode("latin1").split("\\")[-1] for i in range(c24)]

    # scene nodes: identity for every body panel, so a part's mesh data IS its
    # world data.  Wheels carry a 180-degree-about-up node; we keep those out of
    # the flank statistics by only looking at |normal.z| > 0.75 triangles.
    parts = []
    for i in range(c1a):
        r = C1A + i * 288
        parts.append((P[r:r + 12].split(b"\0")[0].decode("latin1"),
                      u32(r + 16), s32(r + 12), u32(r + 40), s32(r + 36)))

    T = []
    for name, V, vb, F, tb in parts:
        vs = []
        for k in range(V):
            o = VBLK + vb + k * 48
            vs.append((struct.unpack("<3f", P[o:o + 12]),
                       struct.unpack("<3f", P[o + 12:o + 24]),
                       struct.unpack("<2f", P[o + 32:o + 40])))
        for k in range(F):
            rec = C20 + tb + k * 76
            ix = [u16(rec + 64 + 2 * j) for j in range(3)]
            if any(x >= V for x in ix):
                continue
            T.append(dict(part=name, tex=s32(rec + 4),
                          pos=[vs[i][0] for i in ix], nrm=[vs[i][1] for i in ix],
                          uv=[vs[i][2] for i in ix]))
    return T, tex


# -------------------------------------------------------------- vector maths --
def sub(a, b): return tuple(x - y for x, y in zip(a, b))
def dot(a, b): return sum(x * y for x, y in zip(a, b))
def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def nrm(a):
    l = math.sqrt(dot(a, a)) or 1.0
    return tuple(x / l for x in a)
def centroid(t): return tuple(sum(p[i] for p in t["pos"]) / 3 for i in range(3))
def tnorm(t): return nrm(tuple(sum(t["nrm"][i][k] for i in range(3)) / 3 for k in range(3)))


def uv_frame(t):
    """The triangle's affine uv -> xyz: returns (du_vec, dv_vec) = world metres per unit uv."""
    Ur = t["uv"]
    e1 = (Ur[1][0] - Ur[0][0], Ur[1][1] - Ur[0][1])
    e2 = (Ur[2][0] - Ur[0][0], Ur[2][1] - Ur[0][1])
    det = e1[0] * e2[1] - e1[1] * e2[0]
    if abs(det) < 1e-12:
        return None
    p1 = sub(t["pos"][1], t["pos"][0]); p2 = sub(t["pos"][2], t["pos"][0])
    du = tuple((p1[i] * e2[1] - p2[i] * e1[1]) / det for i in range(3))
    dv = tuple((p2[i] * e1[0] - p1[i] * e2[0]) / det for i in range(3))
    return du, dv


# --------------------------------------------------------------- measurement 1 --
def matched_pairs(T, body, grid=None):
    """Yield (t, u, pairs) for every flank triangle of texture `body` that has a
    z-mirror partner in the same mesh: u mirrors t in position, and `pairs` are
    the vertex index pairs matched position-to-mirrored-position.  Triangles with
    no partner (the two flanks tessellated differently) are not yielded."""
    if grid is None:
        # index triangles by the rounded z-mirrored centroid
        grid = {}
        for t in T:
            cc = centroid(t)
            key = (round(cc[0], 1), round(cc[1], 1), round(cc[2], 1))
            grid.setdefault(key, []).append(t)
    for t in T:
        if t["tex"] != body:
            continue
        N = tnorm(t)
        cc = centroid(t)
        if abs(N[2]) < 0.75 or abs(cc[2]) < 0.45:
            continue
        # look for the mirror counterpart in a small neighbourhood of the mirrored centroid
        best = None
        for dx in (-0.1, 0.0, 0.1):
            for dy in (-0.1, 0.0, 0.1):
                for dz in (-0.1, 0.0, 0.1):
                    key = (round(cc[0] + dx, 1), round(cc[1] + dy, 1), round(-cc[2] + dz, 1))
                    for u in grid.get(key, []):
                        if u is t or u["tex"] != body:
                            continue
                        Nu = tnorm(u)
                        if Nu[2] * N[2] >= 0:
                            continue
                        # match vertices by mirrored position, then compare uv
                        pairs = []
                        used = set()
                        for i, p in enumerate(t["pos"]):
                            hit = None
                            for j, q in enumerate(u["pos"]):
                                if j in used:
                                    continue
                                if (abs(p[0] - q[0]) < 2e-3 and abs(p[1] - q[1]) < 2e-3
                                        and abs(p[2] + q[2]) < 2e-3):
                                    hit = j; break
                            if hit is not None:
                                used.add(hit); pairs.append((i, hit))
                        if len(pairs) < 2:
                            continue
                        best = (u, pairs, len(pairs))
                    if best:
                        break
                if best:
                    break
            if best:
                break
        if best:
            yield t, best[0], best[1]


def pair_report(car, cars_dir=DEFAULT_CARS, verbose=True):
    """Match triangles of one car against their z-mirrors and report the uv relation."""
    T, tex = parse(os.path.join(cars_dir, car + ".c3d"))
    body = Counter(t["tex"] for t in T).most_common(1)[0][0]

    ident = mir = other = 0
    examples = []
    mir_examples = []
    for t, u, pairs in matched_pairs(T, body):
        n = len(pairs)
        cc = centroid(t)
        same = all(abs(t["uv"][i][0] - u["uv"][j][0]) < 3e-3 for i, j in pairs)
        mirrored = all(abs((1.0 - t["uv"][i][0]) - u["uv"][j][0]) < 3e-3 for i, j in pairs)
        # v: the complement shows up as the two flanks sampling the two halves of
        # a v-symmetric texture; count it as "u mirrored" only when u matches.
        if same:
            ident += 1
            if len(examples) < 4:
                examples.append((t["part"], u["part"], n,
                                 [(round(t["uv"][i][0], 3), round(t["uv"][i][1], 3),
                                   round(u["uv"][j][0], 3), round(u["uv"][j][1], 3)) for i, j in pairs],
                                 round(cc[2], 2), round(centroid(u)[2], 2)))
        elif mirrored:
            mir += 1
            if len(mir_examples) < 2:
                mir_examples.append((t["part"], u["part"], n,
                                     [(round(t["uv"][i][0], 3), round(t["uv"][i][1], 3),
                                       round(u["uv"][j][0], 3), round(u["uv"][j][1], 3)) for i, j in pairs],
                                     round(cc[2], 2), round(centroid(u)[2], 2)))
        else:
            other += 1
    if verbose:
        print(f"# {car}: body/texture[{body}]={tex[body]}  mirrored-pair triangles on the flanks:")
        print(f"  same u at mirrored vertices : {ident}   <-- livery runs the SAME way on both flanks")
        print(f"  u complemented (1-u)        : {mir}   <-- livery UV'd to read on both sides")
        print(f"  neither (different tessellation) : {other}")
        for e in examples:
            print(f"  e.g. {e[0]} (z={e[4]}) <-> {e[1]} (z={e[5]}), {e[2]}/3 vertices matched:")
            for a, b, c2, d in e[3]:
                print(f"        uv (u={a:.3f},v={b:.3f})  <->  (u={c2:.3f},v={d:.3f})"
                      f"   u same={abs(a-c2)<3e-3}  v'=1-v={abs(b-(1-d))<3e-3}")
        for e in mir_examples:
            print(f"  MIRRORED-IN-THE-UVs e.g. {e[0]} (z={e[4]}) <-> {e[1]} (z={e[5]}), {e[2]}/3 matched:")
            for a, b, c2, d in e[3]:
                print(f"        uv (u={a:.3f},v={b:.3f})  <->  (u={c2:.3f},v={d:.3f})"
                      f"   u'=1-u={abs((1.0-a)-c2)<3e-3}")
    return ident, mir, other


# --------------------------------------------------------------- measurement 2 --
def flank_textures(T):
    """texture index sets used by outer left-flank and outer right-flank triangles."""
    L, R = set(), set()
    for t in T:
        N = tnorm(t); cc = centroid(t)
        if abs(N[2]) < 0.75 or abs(cc[2]) < 0.45:
            continue
        (R if cc[2] > 0 else L).add(t["tex"])
    return L, R


def flank_udir(T, body):
    """counts of outer flank triangles by sign of du/dx, per side."""
    cnt = Counter()
    for t in T:
        if t["tex"] != body:
            continue
        N = tnorm(t); cc = centroid(t)
        if abs(N[2]) < 0.75 or abs(cc[2]) < 0.45:
            continue
        fr = uv_frame(t)
        if not fr:
            continue
        du = fr[0]
        l = math.sqrt(dot(du, du)) or 1.0
        ux = du[0] / l
        if abs(ux) < 0.3:
            continue
        cnt[("R" if cc[2] > 0 else "L", "+" if ux > 0 else "-")] += 1
    return cnt


def corpus(cars_dir=DEFAULT_CARS, limit=None):
    files = sorted(f for f in os.listdir(cars_dir) if f.endswith(".c3d"))
    if limit:
        files = files[:limit]
    same_tex = diff_tex = nodata = 0
    body_both = 0
    left_only, right_only = [], []
    same_dir = diff_dir = 0
    tot = Counter()
    for f in files:
        try:
            T, _ = parse(os.path.join(cars_dir, f))
        except Exception:
            nodata += 1; continue
        if not T:
            nodata += 1; continue
        body = Counter(t["tex"] for t in T).most_common(1)[0][0]
        L, R = flank_textures(T)
        if not L or not R:
            nodata += 1; continue
        if L == R:
            same_tex += 1
        else:
            diff_tex += 1
        if body in L and body in R:
            body_both += 1
        elif body in L:
            left_only.append(f[:-4])
        else:
            right_only.append(f[:-4])
        cnt = flank_udir(T, body)
        tot.update(cnt)
        if cnt[("L", "+")] + cnt[("L", "-")] and cnt[("R", "+")] + cnt[("R", "-")]:
            if (cnt[("L", "+")] > cnt[("L", "-")]) == (cnt[("R", "+")] > cnt[("R", "-")]):
                same_dir += 1
            else:
                diff_dir += 1
    print(f"# corpus: {len(files)} .c3d files in {cars_dir}")
    print(f"  flank triangles present : {same_tex + diff_tex}   (no flank data: {nodata})")
    print(f"  the body/livery texture is used by BOTH flanks        : {body_both}")
    print(f"  ...of those, the most-used texture on the LEFT only   : {len(left_only)}"
          + (f"   {left_only}" if left_only else ""))
    print(f"  ...and on the RIGHT only                              : {len(right_only)}"
          + (f"   {right_only}" if right_only else ""))
    print(f"  left flank and right flank name exactly the same texture set : {same_tex}"
          f"   (differ by one panel/underside/glass entry: {diff_tex})")
    print(f"  du/dx sign over outer flank triangles  L+: {tot[('L','+')]}  L-: {tot[('L','-')]}"
          f"   R+: {tot[('R','+')]}  R-: {tot[('R','-')]}")
    print(f"  cars whose two flanks run u the same way : {same_dir}"
          f"   (opposite: {diff_dir})")


# ------------------------------------------------- the 23 exterior car bodies --
def bodies_report(cars_dir=DEFAULT_CARS):
    """The *a1N exterior car files only -- the ones that carry a livery.

    Corpus-wide counts mix these with the *a5 / *c5 cockpit interiors, whose
    most-used texture is a dash/door-card texture and whose flank-band
    triangles are not artwork.  This is the same measurement on the set that
    is actually in the frames."""
    files = sorted(f for f in os.listdir(cars_dir) if f.endswith("a1N.c3d"))
    same_tex = diff_tex = both = 0
    one_sided = []
    ident = mir = other = 0
    sd = dd = 0
    tot = Counter()
    for f in files:
        T, _ = parse(os.path.join(cars_dir, f))
        if not T:
            continue
        body = Counter(t["tex"] for t in T).most_common(1)[0][0]
        L, R = flank_textures(T)
        same_tex += (L == R)
        diff_tex += (L != R)
        if body in L and body in R:
            both += 1
        else:
            one_sided.append(f[:-4])
        cnt = flank_udir(T, body)
        tot.update(cnt)
        if (cnt[("L", "+")] + cnt[("L", "-")]) and (cnt[("R", "+")] + cnt[("R", "-")]):
            if (cnt[("L", "+")] > cnt[("L", "-")]) == (cnt[("R", "+")] > cnt[("R", "-")]):
                sd += 1
            else:
                dd += 1
        i, m, o = pair_report(f[:-4], cars_dir, verbose=False)
        ident += i; mir += m; other += o
    print(f"# a1N exterior bodies: {len(files)} files in {cars_dir}   <- the cars in the frames")
    print(f"  body/livery texture used by BOTH flanks           : {both} / {len(files)}"
          + (f"   ONE-SIDED: {one_sided}" if one_sided else "   (no file uses a one-sided livery)"))
    print(f"  left and right flank name identical texture sets  : {same_tex}"
          f"   (differ by one underside/glass/interior index: {diff_tex})")
    print(f"  mirrored-pair triangles: same u {ident}   u complemented {mir}   neither {other}")
    print(f"  du/dx sign over outer flank triangles  L+: {tot[('L','+')]}  L-: {tot[('L','-')]}"
          f"   R+: {tot[('R','+')]}  R-: {tot[('R','-')]}")
    print(f"  cars whose two flanks run u the same way          : {sd}   (opposite: {dd})")


# ---------------------------------------------------------------------- main --
if __name__ == "__main__":
    a = sys.argv[1:]
    if not a or a[0] not in ("PAIR", "CORPUS", "BODIES", "PAIRS"):
        print(__doc__); sys.exit(2)
    if a[0] == "PAIR":
        if len(a) > 1 and a[1] == "--all":
            files = sorted(f[:-4] for f in os.listdir(DEFAULT_CARS) if f.endswith(".c3d"))
            for car in files:
                try:
                    pair_report(car)
                except Exception as e:
                    print(f"# {car}: {e}")
        else:
            for car in a[1:] or ["205a1N"]:
                pair_report(car)
    elif a[0] == "PAIRS":
        xyz = "--xyz" in a
        for car in [x for x in a[1:] if not x.startswith("--")] or ["205a1N"]:
            T, tex = parse(os.path.join(DEFAULT_CARS, car + ".c3d"))
            body = Counter(t["tex"] for t in T).most_common(1)[0][0]
            print(f"# {car}  body texture[{body}]={tex[body]}  every matched mirrored pair")
            for t, u, pairs in matched_pairs(T, body):
                cz, uz = centroid(t)[2], centroid(u)[2]
                for i, j in pairs:
                    a_, b_ = t["uv"][i]
                    c_, d_ = u["uv"][j]
                    extra = ""
                    if xyz:
                        p_, q_ = t["pos"][i], u["pos"][j]
                        extra = (f"   P=({p_[0]:+.3f},{p_[1]:+.3f},{p_[2]:+.3f})"
                                 f"  P'=({q_[0]:+.3f},{q_[1]:+.3f},{q_[2]:+.3f})")
                    print(f"  {t['part']:14s} z={cz:+.3f} uv=({a_:.3f},{b_:.3f})"
                          f"  <->  {u['part']:14s} z={uz:+.3f} uv=({c_:.3f},{d_:.3f})"
                          f"   u_same={abs(a_ - c_) < 3e-3} v_comp={abs(b_ - (1 - d_)) < 3e-3}{extra}")
    elif a[0] == "CORPUS":
        corpus(a[1] if len(a) > 1 else DEFAULT_CARS)
    else:
        bodies_report(a[1] if len(a) > 1 else DEFAULT_CARS)
