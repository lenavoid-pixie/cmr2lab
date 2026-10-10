#!/usr/bin/env python3
"""car_materials.py -- what the .c3d says about colour, before anything is drawn.

Reads a car .c3d (gzip -> CMPR -> PP_F payload, the same unwrap the viewer does)
and reports, per part:
  * the vertex DIFFUSE and SPECULAR dwords, which the viewer currently drops,
  * the c28 MeshPart records (texture index, min/max index, index count),
  * any material-ish fields in the 288-byte mesh record that are not zero.

usage: car_materials.py CAR.c3d
"""
import collections
import gzip
import struct
import sys

NODE_STRIDE = 396
MESH_STRIDE = 288
TRI_STRIDE = 76
VERT_STRIDE = 48


def payload(path):
    b = open(path, 'rb').read()
    if b[:2] == b'\x1f\x8b':
        g = gzip.decompress(b)
        if g[:4] == b'CMPR':
            sz = struct.unpack('<I', g[4:8])[0]
            return g[8:8 + sz]
        return g
    if b[:4] == b'CMPR':
        sz = struct.unpack('<I', b[4:8])[0]
        return b[8:8 + sz]
    return b


def main():
    P = payload(sys.argv[1])
    assert P[:4] == b'PP_F', P[:8]
    u32 = lambda o: struct.unpack('<I', P[o:o + 4])[0]
    u16 = lambda o: struct.unpack('<H', P[o:o + 2])[0]
    c10, c14 = u32(0x10), u32(0x14)
    c18, c1a, c1c = u16(0x18), u16(0x1a), u16(0x1c)
    c1e = u16(0x1e)
    c28, c26, c24 = u16(0x28), u16(0x26), u16(0x24)
    c20 = u32(0x20)
    print(f"# {sys.argv[1]}  payload {len(P)}  c10={c10} c14={c14} c18={c18} "
          f"c1a={c1a} c1c={c1c} c1e={c1e} c20={c20} c24={c24} c26={c26} c28={c28}")

    C1A = 48 + c18 * NODE_STRIDE
    OBJ = C1A + (c1a + c1c) * MESH_STRIDE
    SEC = OBJ + c1c * 160
    C20 = SEC + c1e * 136 + c28 * 28
    VBLK = C20 + c20 * TRI_STRIDE
    print(f"# C1A={C1A} OBJ={OBJ} SEC={SEC} C20={C20} VBLK={VBLK}")

    for i in range(c1a):
        r = P[C1A + i * MESH_STRIDE: C1A + (i + 1) * MESH_STRIDE]
        name = r[:12].decode('latin1').replace(' ', '')
        V = struct.unpack('<I', r[16:20])[0]
        F = struct.unpack('<I', r[40:44])[0]
        vb = struct.unpack('<i', r[12:16])[0]
        tb = struct.unpack('<i', r[36:40])[0]
        dif = collections.Counter()
        spc = collections.Counter()
        for k in range(V):
            o = VBLK + vb + k * VERT_STRIDE
            if o + 32 > len(P):
                break
            dif[struct.unpack('<I', P[o + 24:o + 28])[0]] += 1
            spc[struct.unpack('<I', P[o + 28:o + 32])[0]] += 1
        # non-zero dwords in the mesh record, to look for a material block
        nz = [f"{j*4:#04x}={struct.unpack('<I', r[j*4:j*4+4])[0]:#010x}"
              for j in range(MESH_STRIDE // 4) if j * 4 not in (0, 4, 8) and
              struct.unpack('<I', r[j*4:j*4+4])[0] not in (0,)]
        print(f"[{i:2d}] {name:12s} V={V:4d} F={F:3d} vb={vb} tb={tb}")
        print(f"     diffuse  {[(hex(v), n) for v, n in dif.most_common(4)]}")
        print(f"     specular {[(hex(v), n) for v, n in spc.most_common(4)]}")
        print(f"     nonzero dwords: {' '.join(nz[:24])}")


main()
