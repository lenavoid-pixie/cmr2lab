#!/usr/bin/env python3
"""cmpru — CMR2 resource unwrapper.
Chain (verified 2026-10-09 against real disc data):
    <file>  --[gzip]-->  "CMPR" + u32 archiveSize + payload
Container per BFL.h: ident[4] + archiveSize; FileBuffer.cpp:103 reads 8 then archiveSize.
"""
import gzip, struct, sys, os

def unwrap(path):
    raw = open(path, 'rb').read()
    gz = raw[:2] == b'\x1f\x8b'
    body = gzip.decompress(raw) if gz else raw
    if body[:4] == b'CMPR':
        size = struct.unpack('<I', body[4:8])[0]
        payload = body[8:8+size]
        ok = (len(body) == 8 + size)
        return dict(gz=gz, cmpr=True, declared=size, actual=len(payload),
                    tail=len(body)-8-size, ok=ok, payload=payload)
    return dict(gz=gz, cmpr=False, payload=body, ok=True)

if __name__ == '__main__':
    for p in sys.argv[1:]:
        r = unwrap(p)
        line = f"{os.path.basename(p):22s} gz={int(r['gz'])} "
        if r['cmpr']:
            line += f"CMPR size={r['declared']:>9,} got={r['actual']:>9,} tail={r['tail']} {'OK' if r['ok'] else 'MISMATCH'}"
            head = r['payload'][:16]
            line += f"  payload[:16]={head.hex()}"
        else:
            line += f"RAW {len(r['payload']):,}"
        print(line)
