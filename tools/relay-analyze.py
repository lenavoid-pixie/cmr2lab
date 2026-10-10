#!/usr/bin/env python3
"""
relay_analyze.py -- turn a WINEDEBUG=+relay capture into a dependency map.

THE TRICK THAT MAKES THIS WORK
------------------------------
Wine's relay log prints the return address of every call, e.g.

    0024:Call user32.FindWindowA(00511430 "Colin McRae Rally 2",...) ret=004a9730

`004a9730` is an address INSIDE CMR2.exe -- and the decomp tree labels every
function with its original address:

    // FUNCTION: CMR2 0x004a9720
    unsigned int CMain::Initialize(...)

So `ret=004a9730` can be resolved to `CMain::Initialize`. That converts the
relay stream from "Win32 calls in order" into

    WHICH DECOMPILED FUNCTION FIRES WHICH WIN32 CALL, IN ORDER

which is the dependency map. Measured, not counted.

Outputs (into the depmap dir):
  runtime_surface.json   per Win32 symbol: runtime sites, calling functions
  static_vs_runtime.md   what the grep thought vs what actually ran
  RUNTIME-RELAY.md       the ordered boot sequence + findings
"""
import os, re, json, sys
from collections import defaultdict, Counter, OrderedDict

TREE = "/home/deck/lena/.lena_cmr2/port/tree/CMR2Decomp"
OUT = "/home/deck/lena/work/CMR2/depmap"
GAME_LO = 0x00400000
GAME_HI = 0x00620000          # CMR2.exe image range (from the trace itself)

FUNC_MARK = re.compile(r'//\s*FUNCTION:\s*CMR2\s+0x([0-9a-fA-F]+)')


FUNCS_TSV = "/home/deck/lena/.lena_cmr2/decomp2/scripts/functions.tsv"


def load_addr_map():
    """addr -> (name, tu, size) from the decomp's own functions.tsv.

    functions.tsv carries ADDRESS AND SIZE for all 3650 functions, so a return
    address can be resolved by exact containment -- no guessing, no nearest-
    marker fallback. (Scraping the `// FUNCTION:` markers instead gave 3363
    entries and mis-resolved addresses that fell in unlabelled gaps, e.g. it
    called a CRT address `InRaceMenu_GetCurtainTexture`.)
    """
    rows = []
    for line in open(FUNCS_TSV, encoding='utf-8', errors='replace'):
        p = line.rstrip('\n').split('\t')
        if len(p) < 3 or p[0] == 'addr':
            continue
        try:
            a = int(p[0], 16)
            sz = int(p[1])
        except ValueError:
            continue
        rows.append((a, sz, p[2]))
    rows.sort()
    return rows


def resolve(rows, addr):
    """Exact containment: addr in [start, start+size)."""
    lo, hi = 0, len(rows) - 1
    best = None
    while lo <= hi:
        mid = (lo + hi) // 2
        if rows[mid][0] <= addr:
            best = rows[mid]
            lo = mid + 1
        else:
            hi = mid - 1
    if best is None:
        return None
    a, sz, nm = best
    if a <= addr < a + sz:
        return dict(addr=a, name=nm, size=sz, delta=addr - a)
    return None


CALL_RE = re.compile(r'^([0-9a-f]{4}):Call\s+([A-Za-z0-9_.\-]+)\.([A-Za-z0-9_]+)\((.*)\)\s+ret=([0-9a-f]+)')

# how far past a function's start an address can land and still be "in" it
MAX_SPAN = 0x4000


def main():
    relay = sys.argv[1] if len(sys.argv) > 1 else os.path.join(OUT, 'relay.relay')
    entries = load_addr_map()
    print(f"functions.tsv entries: {len(entries)}")

    runtime = defaultdict(lambda: dict(sites=0, callers=Counter(), tus=Counter()))
    by_caller = defaultdict(Counter)      # decomp func -> dll.symbol -> count
    seq = []                              # ordered game calls
    lines = 0
    game_calls = 0
    threads = Counter()
    dlls = []

    with open(relay, encoding='utf-8', errors='replace') as fh:
        for line in fh:
            lines += 1
            if line.startswith('#DLL '):
                p = line.split()
                if len(p) >= 3:
                    dlls.append((p[1], p[2]))
                continue
            m = CALL_RE.match(line)
            if not m:
                continue
            tid, dll, sym, args, rethex = m.groups()
            ret = int(rethex, 16)
            if not (GAME_LO <= ret < GAME_HI):
                continue
            game_calls += 1
            threads[tid] += 1
            ent = resolve(entries, ret)
            if ent is None:
                fname, fdelta = f'<unmapped 0x{ret:x}>', None
            else:
                fname, fdelta = ent['name'], ent['delta']
            key = f'{dll}.{sym}'
            e = runtime[key]
            e['sites'] += 1
            e['callers'][fname] += 1
            by_caller[fname][key] += 1
            seq.append(dict(n=game_calls, tid=tid, dll=dll, symbol=sym,
                            args=args[:200], ret=rethex,
                            func=fname, delta=fdelta))

    print(f"relay lines          : {lines}")
    print(f"game-image calls     : {game_calls}")
    print(f"distinct game funcs  : {len(by_caller)}")
    print(f"distinct symbols     : {len(runtime)}")
    print(f"threads involved     : {dict(threads)}")
    print()

    out = dict(
        relay_lines=lines, game_calls=game_calls,
        distinct_functions=len(by_caller), distinct_symbols=len(runtime),
        threads=dict(threads),
        dll_load_order=[f"{r} {n}" for n, r in dlls],
        surface={k: dict(sites=v['sites'], callers=dict(v['callers']))
                 for k, v in runtime.items()},
        by_caller={k: dict(v) for k, v in by_caller.items()},
        sequence=seq,
    )
    json.dump(out, open(os.path.join(OUT, 'runtime_surface.json'), 'w'), indent=1)

    print("=== RUNTIME SURFACE, top 30 (symbol / runtime sites / calling decomp funcs) ===")
    for k, v in sorted(runtime.items(), key=lambda kv: -kv[1]['sites'])[:30]:
        c = ', '.join(f"{n}*{c}" for n, c in v['callers'].most_common(3))
        print(f"  {v['sites']:5d}  {k:34s} {c}")
    print()
    print("=== DECOMP FUNCTIONS THAT ACTUALLY FIRED WIN32 (top 25) ===")
    for k, v in sorted(by_caller.items(), key=lambda kv: -sum(kv[1].values()))[:25]:
        print(f"  {sum(v.values()):5d}  {k:42s} " +
              ', '.join(f"{s}*{c}" for s, c in v.most_common(4)))
    return out


if __name__ == '__main__':
    main()
