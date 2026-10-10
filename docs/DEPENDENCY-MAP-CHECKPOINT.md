# CHECKPOINT — dependency map (static + runtime), 2026-10-10

Round complete. Everything below is on disk and reproducible.

## What was asked, what was done

1. **STATIC call graph** — DONE.
2. **RUNTIME relay trace** — DONE, and it found the gate that mattered.
3. **The ordering** — DONE, seven tiers, each backed by a runtime count.

## Where things live

| artifact | path |
|---|---|
| tools | `~/lena/work/CMR2/depmap/{depmap,relay_filter,relay_scan,relay_analyze,depmap_report}.py` |
| runners | `~/lena/work/CMR2/depmap/relay-run.sh`, `relay-run2.sh` |
| data | `depmap.json`, `runtime_surface_run5.json`, 5 CSVs |
| docs | `DEPENDENCY-MAP.md`, `RUNTIME-RELAY.md` |
| published | `~/lena/work/cmr2lab-publish` — 2 commits (`fbecb47`, `a54c112`), **committed locally, NOT pushed** |
| relay prefix | `~/lena/work/CMR2/depmap/pfx-relay` (343 MB, a COPY — his `~/.cmr2proton/pfx` untouched) |

## Headline numbers

* 85 distinct Win32 symbols / 219 sites / 66 TUs (census said **47** — narrow regex).
* Runtime: 375 game-image calls, 46 symbols, **23 decompiled functions** fire
  before a first frame.
* **15 Win32 symbols fire at runtime that the static census never counted**;
  `DefWindowProcA`\*39 and `RegisterWindowMessageA`\*30 are the big ones.
* D3D7/COM surface is 278 / 298 / 518 by scope (`pD3D->` / +`pDD->` / all typed
  receivers). The 298 matched his number exactly — it is scope, not a correction.

## The finding that changes the plan

With his prefix untouched, the shipped `CMR2.exe` makes **669 Win32 calls and
exits after one second**, no `ddraw`, no `dsound`, no `dinput`, no asset reads,
and writes `c:\error.txt` = "Program finished normally".

Cause: `CGame::InitializeGame` (Game.cpp:991) reads `Sku_Type` from
`HKLM\SOFTWARE\Codemasters\Colin McRae Rally 2`; if none of
EUROPE/AMERICA/JAPAN/POLAND match it calls `CGame::SetShouldExit()`. Then
Game.cpp:1007 needs `Game_HDPath` / `Game_CDPath` / `Install_Version`.

**A broken registry shim fails silently, not loudly.** Highest-leverage piece in
the layer, and the cheapest to write.

Gotchas worth carrying forward:
* 32-bit registry redirection — the key must go in
  `HKLM\SOFTWARE\WOW6432Node\...`; `reg query` reads the unredirected path and
  lies to you.
* Relay `ret=` addresses resolve against
  `~/.lena_cmr2/decomp2/scripts/functions.tsv` (addr **and size**, 3,649 rows).
  Do NOT scrape the `// FUNCTION:` markers — 3,363 rows with gaps, and it
  mis-attributes (called a CRT address `InRaceMenu_GetCurtainTexture`).
* `RelayFromExclude` default covers user32/gdi32/advapi32/kernel32 — suppresses
  calls *out of* those DLLs, not the game's calls *into* them.

## Where the measurement currently stops

`CGraphics::InitializeDirectX` runs, `DirectDrawCreateEx` returns `DD_OK`, and
then the game blocks on a **modal `MessageBoxA("Setting configuration to
defaults")`**. The 50 s run sat on it. So everything below tier 6 — the D3D7
device, the renderer, `Game_DrawSceneViewport` — is still unmeasured at runtime.

## Next round, in order

1. **Dismiss that dialog** and re-trace to get the real running surface.
   `relay-run2.sh` already tries, but its xdotool loop matched nothing
   (`dismissals=0`) and the dialog was dismissed by something else. Fix: find the
   dialog's window by the wine PID via `xdotool search --pid`, or send `Return`
   through `xdotool key --window <id>` after enumerating children with
   `xdotool search --all --pid`. The payload to look for is
   `ddraw.IDirectDraw7::CreateDevice` / `QueryInterface` followed by
   `SetTextureStageState` / `DrawIndexedPrimitiveVB`.
2. **Merge runtime into the static graph**: for each decompiled function, the
   set of Win32 it fires measured vs counted, so tier promotion is evidence-based
   rather than argued.
3. **The 1,258 class-C errors** (`(int)pointer`) are untouched by this round.
   `PLATFORM-DECISION.md` already answers them by going i386; the dependency
   question left open is which *structs* they cluster in, the same treatment §2
   gave class A.
