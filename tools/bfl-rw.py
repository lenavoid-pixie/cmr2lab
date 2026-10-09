#!/usr/bin/env python3
"""bflpack.py - CMR2 .bfl read AND write. Round-trip proven.

LAYOUT (derived from bfl.py read + verified against 205a1.bfl):
    [0 .. 8)        b"CMPR" + u32(containerSize)   ; containerSize = L - 8
    [8 .. 8+D)      file data, concatenated
    [8+D .. L-4)    TOC entries
    [L-4 .. L)      u32 tocPointer  ; == D  (TOC starts at tocPointer+8)
  entry: u32 size; u32 offset; u32 strLen; char name[strLen]; pad to 4
  absolute file data offset = entry.offset + 8
"""
import gzip, struct, io, os

MAGIC = b'CMPR'

def load(path):
    raw = open(path, 'rb').read()
    return gzip.decompress(raw) if raw[:2] == b'\x1f\x8b' else raw

def parse(blob):
    if blob[:4] != MAGIC: raise ValueError("not CMPR")
    csize = struct.unpack('<I', blob[4:8])[0]
    toc_ptr = struct.unpack('<I', blob[csize+4:csize+8])[0]
    pos = toc_ptr + 8
    out = []
    while pos < len(blob) - 4:
        size, offset, slen = struct.unpack('<III', blob[pos:pos+12]); pos += 12
        name = blob[pos:pos+slen].rstrip(b'\0').decode('ascii', 'replace')
        pos += slen + ((4 - slen % 4) % 4)
        out.append((name, offset, size))
    return out

def extract(blob):
    """-> [(name, bytes), ...] in TOC order"""
    return [(n, blob[o+8:o+8+s]) for (n, o, s) in parse(blob)]

def build(entries, header=8):
    """entries: [(name, bytes), ...] -> raw CMPR container (uncompressed)."""
    data = bytearray(); toc = bytearray()
    for name, content in entries:
        while len(data) % 4:            # ★ DATA AREA IS 4-BYTE ALIGNED BETWEEN ENTRIES
            data.append(0)
        off = len(data)
        data += content
        nb = name.encode('ascii')
        toc += struct.pack('<III', len(content), off, len(nb))
        toc += nb
        toc += b'\0' * ((4 - len(nb) % 4) % 4)
    while len(data) % 4:                # ★ AND THE TOC ITSELF STARTS 4-ALIGNED
        data.append(0)
    D = len(data)
    L = 8 + D + len(toc) + 4
    out = bytearray()
    out += MAGIC + struct.pack('<I', L - 8)
    out += data
    out += toc
    out += struct.pack('<I', D)          # tocPointer
    assert len(out) == L
    return bytes(out)

def save(path, entries, compress=True):
    raw = build(entries)
    open(path, 'wb').write(gzip.compress(raw, 9) if compress else raw)
    return len(raw)

if __name__ == '__main__':
    import sys
    src = sys.argv[1] if len(sys.argv) > 1 else '/root/.lena_cmr2/game/Game/Cars/205a1.bfl'
    blob = load(src)
    ents = extract(blob)
    print(f"read {os.path.basename(src)}: {len(ents)} entries, {len(blob):,} bytes")

    rebuilt = build(ents)
    print(f"rebuilt: {len(rebuilt):,} bytes")
    print(f"★ BYTE-IDENTICAL TO ORIGINAL? {rebuilt == blob}")
    if rebuilt != blob:
        for i,(a,b) in enumerate(zip(rebuilt, blob)):
            if a != b:
                print(f"   first diff at byte {i}: rebuilt={a:#x} original={b:#x}")
                print(f"   context rebuilt : {rebuilt[max(0,i-8):i+8].hex()}")
                print(f"   context original: {blob[max(0,i-8):i+8].hex()}")
                break
    # and full extraction equality
    e2 = extract(rebuilt)
    print(f"★ extraction identical? {[ (n,d) for n,d in ents ] == e2}")
