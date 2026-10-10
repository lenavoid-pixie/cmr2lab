#!/usr/bin/env python3
"""
relay_scan.py -- scan a +relay capture DIRECTLY for calls the game itself made.

The live filter caps its output, which truncates long runs. This scan has no
cap: it keeps only lines whose return address lands inside CMR2.exe
(0x400000-0x620000), which is the game's own code, and resolves that address to
a decompiled function via functions.tsv.

Usage: relay_scan.py RAWFILE [OUTJSON]
"""
import re, sys, json, os
from collections import defaultdict, Counter

FUNCS_TSV = "/home/deck/lena/.lena_cmr2/decomp2/scripts/functions.tsv"
GAME_LO, GAME_HI = 0x00400000, 0x00620000

CALL_RE = re.compile(
    r'^([0-9a-f]{4}):Call\s+([A-Za-z0-9_.\-]+)\.([A-Za-z0-9_]+)\((.*)\)\s+ret=([0-9a-f]+)')


def load_rows():
    rows = []
    for line in open(FUNCS_TSV, encoding='utf-8', errors='replace'):
        p = line.rstrip('\n').split('\t')
        if len(p) < 3 or p[0] == 'addr':
            continue
        try:
            rows.append((int(p[0], 16), int(p[1]), p[2]))
        except ValueError:
            pass
    rows.sort()
    return rows


def resolve(rows, addr):
    lo, hi, best = 0, len(rows) - 1, None
    while lo <= hi:
        mid = (lo + hi) // 2
        if rows[mid][0] <= addr:
            best, lo = rows[mid], mid + 1
        else:
            hi = mid - 1
    if best and best[0] <= addr < best[0] + best[1]:
        return best[2]
    return None


def main():
    raw = sys.argv[1]
    outp = sys.argv[2] if len(sys.argv) > 2 else '/tmp/relay_scan.json'
    rows = load_rows()
    surface = defaultdict(lambda: dict(sites=0, callers=Counter()))
    by_caller = defaultdict(Counter)
    seq = []
    n = 0
    lines = 0
    with open(raw, encoding='utf-8', errors='replace') as fh:
        for line in fh:
            lines += 1
            if ':Call ' not in line:
                continue
            m = CALL_RE.match(line)
            if not m:
                continue
            tid, dll, sym, args, rethex = m.groups()
            ret = int(rethex, 16)
            if not (GAME_LO <= ret < GAME_HI):
                continue
            fn = resolve(rows, ret) or f'<unmapped 0x{ret:x}>'
            n += 1
            key = f'{dll}.{sym}'
            surface[key]['sites'] += 1
            surface[key]['callers'][fn] += 1
            by_caller[fn][key] += 1
            seq.append(dict(n=n, tid=tid, dll=dll, symbol=sym,
                            args=args[:200], ret=rethex, func=fn))
    out = dict(raw=raw, raw_lines=lines, game_calls=n,
               surface={k: dict(sites=v['sites'], callers=dict(v['callers']))
                        for k, v in surface.items()},
               by_caller={k: dict(v) for k, v in by_caller.items()},
               sequence=seq)
    json.dump(out, open(outp, 'w'), indent=1)

    print(f"raw lines      : {lines}")
    print(f"game-image calls: {n}")
    print(f"distinct symbols: {len(surface)}")
    print(f"distinct funcs  : {len(by_caller)}")
    print()
    print("=== RUNTIME SURFACE (symbol / sites / calling decomp functions) ===")
    for k, v in sorted(surface.items(), key=lambda kv: -kv[1]['sites']):
        c = ', '.join(f"{a}*{b}" for a, b in v['callers'].most_common(3))
        print(f"  {v['sites']:6d}  {k:34s} {c}")
    print()
    print("=== DECOMP FUNCTIONS THAT FIRED WIN32, in order of first firing ===")
    seen = []
    for e in seq:
        if e['func'] not in seen:
            seen.append(e['func'])
    for i, f in enumerate(seen, 1):
        print(f"  {i:3d}  {f:40s} {sum(by_caller[f].values()):5d} calls")


if __name__ == '__main__':
    main()
