#!/usr/bin/env python3
"""d3d_census.py -- ONE tool that settles two arguments with one run.

  1. THE D3D7 CALL CENSUS, WITH ITS SCOPE PRINTED BESIDE IT.
     `docs/PORT-PLAN.md` publishes 278 call sites / 20 methods (pD3D-> only);
     `progress.md` quotes 277 of 298 render-state. Those are two different
     RECEIVERS, not two different answers, and the published copies lost the
     qualifying words. This prints every scope side by side, per method, from
     the decomp source, so a stranger can check all three numbers at once.

  2. THE STATE GAP. Every render state and texture-stage state the game names,
     against the ones the port's device actually maps to the RHI. A state that
     is not mapped is not a crash -- deck_dd7.cpp returns D3D_OK and counts it --
     it is a wrong picture, which is worse.

USAGE
  python3 d3d_census.py                    # the tree and RHI as checked out here
  python3 d3d_census.py --tree DIR --rhi FILE [--json OUT]

Stdlib only. Reads source text. Invents nothing.
"""
import argparse, json, os, re, sys
from collections import Counter, defaultdict

TREE = "/home/deck/lena/.lena_cmr2/port/tree/CMR2Decomp"
RHI = "/home/deck/lena/.lena_cmr2/port/rhi/deck_dd7.cpp"
HDR = "/home/deck/lena/.lena_cmr2/port/platform/dx7/dx7compat.h"

# The IDirect3DDevice7 / IDirectDraw7 / IDirectDrawSurface7 / IDirect3D7 /
# IDirect3DVertexBuffer7 methods, from the DX7 SDK's own interface order.
METHODS = """
BeginScene EndScene Clear SetTransform GetTransform SetViewport GetViewport
SetMaterial GetMaterial SetLight GetLight LightEnable GetLightEnable
SetRenderState GetRenderState SetTextureStageState GetTextureStageState
ValidateDevice SetClipStatus GetClipStatus GetTexture SetRenderTarget GetRenderTarget
DrawPrimitive DrawIndexedPrimitive DrawPrimitiveVB DrawIndexedPrimitiveVB
MultiplyTransform SetTextureStageState CreateStateBlock CaptureStateBlock
ApplyStateBlock DeleteStateBlock BeginStateBlock EndStateBlock PreLoad
GetCaps GetInfo EnumTextureFormats DrawPrimitive DrawIndexedPrimitive
SetDisplayMode GetDisplayMode GetCaps SetCooperativeLevel CreateSurface
CreateClipper GetDeviceIdentifier WaitForVerticalBlank FlipToGDISurface
RestoreDisplayMode RestoreAllSurfaces TestCooperativeLevel GetGDISurface
EnumDisplayModes EnumSurfaces GetAvailableVidMem GetFourCCCodes GetScanLine
GetMonitorFrequency GetVerticalBlankStatus GetSurfaceFromDC
CreateVertexBuffer Lock Unlock Optimize GetVertexBufferDesc ProcessVertices
Blend Blt BltFast BltBatch Flip SetClipper GetClipper GetAttachedSurface
AddAttachedSurface SetColorKey GetColorKey IsLost Restore GetDC ReleaseDC
GetSurfaceDesc GetPixelFormat SetPalette GetPalette SetHWnd GetHWnd SetSurfaceDesc
EnumZBufferFormats EvictManagedTextures EnumDevices CreateDevice SetLight
""".split()


def strip_comments(s):
    s = re.sub(r"/\*.*?\*/", " ", s, flags=re.S)
    return re.sub(r"//[^\n]*", " ", s)


def cxx_sources(tree):
    for fn in sorted(os.listdir(tree)):
        if fn.endswith((".cpp", ".h")):
            yield fn, open(os.path.join(tree, fn), errors="replace").read()


# The receiver is the LAST hop of a chain: `m_pTextureManager->pD3D->SetRenderState`
# counts as a pD3D-> site, which is what the published scope means. A regex that
# stops at the first identifier silently misses every one of those -- measured,
# that mistake is worth 275 vs 278 and it is exactly how a census stops meaning
# anything.
CALL = re.compile(r"([A-Za-z_]\w*(?:\s*->\s*[A-Za-z_]\w*)*)\s*->\s*([A-Za-z_]\w*)\s*\(")

DRAW_METHODS = {"DrawPrimitive", "DrawIndexedPrimitive", "DrawPrimitiveVB",
                "DrawIndexedPrimitiveVB"}


def census(tree):
    per_method = defaultdict(Counter)      # method -> receiver -> n
    per_recv = Counter()
    per_file = Counter()
    for fn, raw in cxx_sources(tree):
        src = strip_comments(raw)
        for m in CALL.finditer(src):
            last = re.sub(r"\s+", "", m.group(1).split("->")[-1])
            per_method[m.group(2)][last] += 1
            per_recv[last] += 1
            per_file[fn] += 1
    return per_method, per_recv, per_file


def scopes(per_recv, per_method):
    """the published scopes, measured -- and the arithmetic that reconciles them."""
    out = {}
    for label, recvs in (("all", None), ("pD3D+pDD", ("pD3D", "pDD")), ("pD3D", ("pD3D",))):
        n = meths = 0
        methods = Counter()
        for meth, rec in per_method.items():
            for r, c in rec.items():
                if recvs is None or r in recvs:
                    n += c
                    methods[meth] += c
        draws = sum(c for m, c in methods.items() if m in DRAW_METHODS)
        out[label] = {"sites": n, "methods": len(methods), "draws": draws,
                      "state": n - draws, "by_method": dict(methods)}
    out["receivers"] = dict(per_recv.most_common())
    return out


def enum_values(path, prefix):
    out = {}
    for m in re.finditer(r"\b(" + prefix + r"[A-Z0-9_]*)\s*=\s*(0x[0-9a-fA-F]+|\d+)",
                         open(path, errors="replace").read()):
        try:
            out[m.group(1)] = int(m.group(2), 0)
        except ValueError:
            pass
    return out


def used_states(tree):
    sites = defaultdict(list)
    for fn, raw in cxx_sources(tree):
        src = strip_comments(raw)
        for chunk in re.split(r"\n(?=[A-Za-z_].*\n\{)", src):
            fm = re.match(r"[A-Za-z_][^\n]*?([A-Za-z_]\w*)\s*\(", chunk)
            func = fm.group(1) if fm else "?"
            for m in re.finditer(r"SetRenderState\s*\(\s*(D3DRENDERSTATE_[A-Z0-9_]+)", chunk):
                sites[("RS", m.group(1))].append((fn, func))
            for m in re.finditer(r"SetTextureStageState\s*\([^,]*,\s*(D3DTSS_[A-Z0-9_]+)", chunk):
                sites[("TSS", m.group(1))].append((fn, func))
    return sites


def mapped(rhi, which):
    txt = strip_comments(open(rhi, errors="replace").read())
    key = "static int map_" + which
    if key not in txt:
        return set()
    body = txt[txt.index(key):]
    body = body[:body.index("\n}\n")]
    return set(re.findall(r"\b(D3D(?:RENDERSTATE|TSS)_[A-Z0-9_]+)", body))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tree", default=TREE)
    ap.add_argument("--rhi", default=RHI)
    ap.add_argument("--hdr", default=HDR)
    ap.add_argument("--json")
    a = ap.parse_args()

    per_method, per_recv, per_file = census(a.tree)
    sc = scopes(per_recv, per_method)

    print("=== 1. THE D3D7 CALL CENSUS, EVERY SCOPE PRINTED TOGETHER ===")
    print("source: %s  (%d .cpp/.h)" % (a.tree, len(list(cxx_sources(a.tree)))))
    print()
    print("scope                            sites  methods  draws  state(=sites-draws)")
    for label in ("all", "pD3D+pDD", "pD3D"):
        d = sc[label]
        print("%-32s %5d  %7d  %5d  %5d" % (label, d["sites"], d["methods"], d["draws"], d["state"]))
    print()
    print("and the arithmetic the README and progress.md quote:")
    print("  pD3D-> only        : %d sites, %d methods, %d draws, %d state"
          % (sc["pD3D"]["sites"], sc["pD3D"]["methods"], sc["pD3D"]["draws"], sc["pD3D"]["state"]))
    print("  pD3D-> + pDD->     : %d sites, %d methods, %d draws, %d state"
          % (sc["pD3D+pDD"]["sites"], sc["pD3D+pDD"]["methods"], sc["pD3D+pDD"]["draws"],
             sc["pD3D+pDD"]["state"]))
    print()
    print("receivers that carry D3D7 calls (this is what the scopes mean):")
    for r, c in per_recv.most_common():
        print("   %-24s %5d" % (r, c))
    print()
    print("%-28s %5s %6s %6s %s" % ("method", "pD3D", "pD3D+pDD", "all", "kind"))
    methods = sorted(per_method.items(), key=lambda kv: -sum(kv[1].values()))
    for meth, rec in methods:
        p3 = rec.get("pD3D", 0); pdd = rec.get("pDD", 0)
        kind = "DRAW" if meth in DRAW_METHODS else "state/other"
        if p3 or pdd:
            print("%-28s %5d %6d %6d %s" % (meth, p3, p3 + pdd, sum(rec.values()), kind))
    print("sites per file, top 8:")
    for f, c in per_file.most_common(8):
        print("   %-28s %5d" % (f, c))

    sites = used_states(a.tree)
    rs_map, tss_map = mapped(a.rhi, "rs"), mapped(a.rhi, "tss")
    inv_rs = {}
    for k, v in enum_values(a.hdr, "D3DRENDERSTATE_").items():
        inv_rs.setdefault(v, k)

    print()
    print("=== 2. THE STATE GAP (game's own source -> the port's map_rs/map_tss) ===")
    print("SetRenderState named sites      : %d (%d distinct)"
          % (sum(len(v) for k, v in sites.items() if k[0] == "RS"),
             len([k for k in sites if k[0] == "RS"])))
    print("SetTextureStageState named sites: %d (%d distinct)"
          % (sum(len(v) for k, v in sites.items() if k[0] == "TSS"),
             len([k for k in sites if k[0] == "TSS"])))
    unmapped = []
    for kind, label, table in (("RS", "RENDER STATE", rs_map), ("TSS", "TEXTURE STAGE", tss_map)):
        print()
        print("--- %s ---" % label)
        for key in sorted([k for k in sites if k[0] == kind], key=lambda k: -len(sites[k])):
            name, n = key[1], len(sites[key])
            ok = "mapped" if name in table else "*** DROPPED ***"
            print("%-36s %4d  %s" % (name, n, ok))
            if name not in table:
                unmapped.append((name, n, sites[key]))
    print()
    print("=== the gap, with the function that needs it ===")
    if not unmapped:
        print("none: every state the game names is mapped onto the RHI")
    for name, n, where in sorted(unmapped, key=lambda x: -x[1]):
        funcs = Counter(f for _, f in where)
        print("%-36s %4d  %s" % (name, n, ", ".join("%s(%d)" % (f, c) for f, c in funcs.most_common(3))))
    print()
    print("map_rs answers %d render states, map_tss answers %d stage states." %
          (len(rs_map), len(tss_map)))

    if a.json:
        json.dump({"scopes": sc,
                   "per_method": {k: dict(v) for k, v in per_method.items()},
                   "per_file": dict(per_file),
                   "states_used": {("%s/%s" % k): len(v) for k, v in sites.items()},
                   "unmapped": [n for n, _, _ in unmapped],
                   "map_rs": sorted(rs_map), "map_tss": sorted(tss_map)},
                  open(a.json, "w"), indent=1, sort_keys=True)
        print("json: %s" % a.json)
    return 0


if __name__ == "__main__":
    sys.exit(main())
