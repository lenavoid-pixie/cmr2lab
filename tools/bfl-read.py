#!/usr/bin/env python3
"""bfl.py - CMR2 .bfl archive reader.
Format per MarkusMaal/BFLExtractCsharp patterns/bfl.hexpat (verified spec):
    "CMPR" | u32 containerSize        (header, 8 bytes)
    ... data ...
    u32 tocPointer  @ containerSize+4-4   <- LAST 4 BYTES OF FILE
    TOC @ tocPointer+8: entries of {u32 size; u32 offset; u32 strLen; char name[strLen]; pad to 4}
Absolute file data offset = entry.offset + 8.
"""
import gzip, struct, sys, os

def load(path):
    raw = open(path,'rb').read()
    return gzip.decompress(raw) if raw[:2]==b'\x1f\x8b' else raw

def read_entries(blob, header=8):
    if blob[:4]!=b'CMPR': raise ValueError("not CMPR")
    csize = struct.unpack('<I', blob[4:8])[0]
    toc_ptr = struct.unpack('<I', blob[csize+header-4:csize+header])[0]
    out=[]; pos = toc_ptr+header
    while pos < len(blob)-4:
        size,offset,slen = struct.unpack('<III', blob[pos:pos+12]); pos+=12
        slen_p = slen + ((4 - slen%4) % 4)
        name = blob[pos:pos+slen].rstrip(b'\0').decode('ascii','replace'); pos += slen_p
        out.append(dict(size=size, offset=offset, name=name))
    return csize, toc_ptr, out

if __name__=='__main__':
    for path in sys.argv[1:]:
        blob = load(path)
        csize, toc_ptr, ents = read_entries(blob)
        print(f"\n{'='*74}\n{os.path.basename(path)}")
        print(f"  raw={os.path.getsize(path):,}  decompressed={len(blob):,}  containerSize={csize:,}  tocPtr={toc_ptr:,}")
        print(f"  {len(ents)} entries")
        tot=0
        for e in ents[:14]:
            print(f"    {e['size']:>9,}  @{e['offset']:>9,}   {e['name']}")
            tot+=e['size']
        if len(ents)>14: print(f"    ... +{len(ents)-14} more")
        # sanity: does the last entry's data fit?
        last = ents[-1]
        print(f"  last entry ends at {last['offset']+8+last['size']:,} (file is {len(blob):,})")
