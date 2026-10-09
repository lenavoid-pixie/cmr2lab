#!/usr/bin/env python3
"""c3d.py — Colin McRae Rally 2.0 / PP_F geometry reader.

Header map taken from the loader at CMR2.exe 0x004B93C0 (the function the
"PP_F" strncmp at 0x004B9389 calls when the magic matches). Data begins at
+0x30 and is a run of typed arrays, each preceded by its count in the header.
"""
import struct, gzip, os

HDR = 0x30
BLOCKS = [                       # (offset, width, stride)  in file order
    (0x0c, 32, 0),
    (0x10, 32, 20),
    (0x14, 32, 52),
    (0x18, 16, 396),
    (0x1a, 16, 288),
    (0x1c, 16, 288),
    (0x1e, 16, 136),
    (0x20, 32, 76),
    (0x24, 16, 0),
    (0x26, 16, 92),
    (0x28, 16, 28),
]

def load(path):
    raw = open(path,'rb').read()
    b = gzip.decompress(raw) if raw[:2]==b'\x1f\x8b' else raw
    return b[8:] if b[:4]==b'CMPR' else b

def read(p):
    if p[:4]!=b'PP_F': raise ValueError('not PP_F')
    counts = {}
    for off,width,stride in BLOCKS:
        v = struct.unpack('<H' if width==16 else '<I', p[off:off+(width//8)])[0]
        counts[off] = v
    # array bytes, in the order the loader walks them
    arr = 0
    spans = {}
    spans['0x0c'] = None                                   # a raw offset, no size
    for key,stride in [('0x18',396),('0x1a',288),('0x1c',288),('0x1e',136),
                       ('0x28',28),('0x20',76),('0x14',52),('0x10',20),('0x26',92)]:
        off = int(key,16); n = counts[off]; arr += n*stride
        spans[key] = arr
    return counts, arr, spans

def report(path):
    p = load(path)
    counts, arr, spans = read(p)
    names = {'0x0c':'rawOffset','0x10':'c10','0x14':'c14','0x18':'c18','0x1a':'c1a',
             '0x1c':'c1c','0x1e':'c1e','0x20':'c20','0x24':'TEXTURES','0x26':'c26','0x28':'c28'}
    N=len(p)
    data_end = HDR + arr
    # where do the trailing tables sit?
    tex_name_off = data_end
    path_off     = tex_name_off + counts[0x24]*24
    path_end     = path_off + counts[0x24]*260
    print(f"{os.path.basename(path):14s} N={N:>8,}  header+arrays={data_end:>8,}", end='')
    ok = (path_end == N)
    print(f"  paths end={path_end:>8,}  {'★ EXACT' if ok else '✗ MISMATCH'}")
    return ok, counts, arr, data_end, path_end, N

if __name__=='__main__':
    import sys, os
    CARS='/root/.lena_cmr2/game/Game/Cars'
    fs = sorted(f for f in os.listdir(CARS) if f.lower().endswith('.c3d'))
    go=0; bad=[]
    for f in fs:
        try:
            ok,*_ = report(os.path.join(CARS,f))
            go += ok
            if not ok: bad.append(f)
        except Exception as e:
            bad.append(f"{f} {e}")
    print(f"\n{'='*70}\n★ {go} / {len(fs)} car models close EXACTLY")
    for b in bad[:8]: print("   ✗", b)
