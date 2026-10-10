# RUNTIME WIN32 SURFACE — MEASURED, NOT COUNTED

Wine is the reference implementation of the exact layer the port is converting
from, and it is already on this machine. Running the original `CMR2.exe` with
`WINEDEBUG=+relay` prints every Win32 call, in order, on the real code path.

Everything below is from that trace. Reproduce with:

```
cd ~/lena/work/CMR2/depmap
./relay-run2.sh 50 /tmp/relay5.raw        # runs against pfx-relay (see caveats)
python3 relay_scan.py /tmp/relay5.raw runtime_surface_run5.json
```

---

## 1. The mechanism that makes this readable

Relay lines look like:

```
0024:Call user32.FindWindowA(00511430 "Colin McRae Rally 2",...) ret=004a9730
```

`004a9730` is a return address **inside CMR2.exe**, and the decomp's
`functions.tsv` carries address *and size* for all 3,650 functions. So every
call resolves to a decompiled function by exact containment:

```
0x4a9730 -> CMain::Initialize   (0x4a9720 + 385 bytes)
```

That converts the trace from *"Win32 calls in order"* into
**"which decompiled function fires which Win32 call, in order."**

First attempt used the `// FUNCTION: CMR2 0x...` markers instead. That was
wrong and worth recording: it yields only 3,363 entries with gaps, and it
resolved a CRT address to `InRaceMenu_GetCurtainTexture`. `functions.tsv` is the
authoritative map. Use it.

**Proton does pass `WINEDEBUG` through** — `proton` uses `setdefault`, so an
exported `WINEDEBUG=+relay` survives. But calling
`files/bin/wine` directly against a prefix keeps stdout/stderr under our control
and avoids `PROTON_LOG` interleaving, so that is what the scripts do.

## 2. First result: the shipped exe never reaches DirectDraw

Plain run, his prefix untouched: **669 Win32 calls, then `exit(0)` after 1
second.** No `ddraw`, no `dsound`, no `dinput`, no file reads. The game's own log
`c:\error.txt` says:

```
FILE_PRINT DEBUG INFORMATION
* Program finished normally *
```

So `CMain::Initialize` ran to completion — it created the window, pumped, and
left. The pump used **`PeekMessageA` twice, `GetMessageA` zero times,
`DispatchMessageA` zero times.**

`CGame::SetShouldExit()` is what ends it, and it is called from exactly three
places. The trace pinned it to the first one:

| gate | location | condition | what it needs |
|---|---|---|---|
| **G1 SKU** | `CGame::InitializeGame` Game.cpp:991 | none of EUROPE/AMERICA/JAPAN/POLAND match | `Sku_Type` |
| **G2 install** | `CGame::InitializeGame` Game.cpp:1007 | `CInstallInfo::LoadInstallPathsFromRegistry()==0` | `Game_HDPath`, `Game_CDPath`, `Install_Version` |
| G3 | Game.cpp:1023/1025/1034/1036 | network / splash / fonts / languages == 0 | assets |
| G4 | `Game_FinishRaceAndAdvanceBootState` Game.cpp:825 | fallthrough | — |

### The trap: 32-bit registry redirection

Setting `HKLM\SOFTWARE\Codemasters\Colin McRae Rally 2` made `reg query` report
the key correctly — **and the game still got `ERROR_FILE_NOT_FOUND`** on
`RegOpenKeyExA(HKLM\SOFTWARE, "Codemasters")`. The game is a 32-bit PE32 binary;
Wine redirects *its* `HKLM\SOFTWARE` to `HKLM\SOFTWARE\WOW6432Node`. `reg` runs
64-bit and reads the unredirected path.

The key has to go in **`HKLM\SOFTWARE\WOW6432Node\Codemasters\Colin McRae Rally 2`**.
This is exactly the class of thing a static grep cannot tell you, and it is a
direct warning for the port's own `CRegKey::GetValueFromKey` shim.

## 3. Measured firing order to first frame

23 decompiled functions fired, in this order (50 s run, 375 game-image calls):

```
 1  entry                                        8 calls   CRT startup
 2  Game_SetDoublePrecisionFPU                   1
 3  CMain::Initialize                            3         FindWindowA, PeekMessageA*2
 4  CLogger::OpenLogFile                         2         lstrcpyA, CreateFileA
 5  CLogger::LogToFile                          15         lstrlenA, WriteFile, FlushFileBuffers
 6  FUN_004ce163                               142         realloc*, memmove*
 7  CMain::CreateGameWindow                     10         RegisterClassA, CreateWindowExA, ...
 8  CMain::MessageHandler                       69         RegisterWindowMessageA*30, DefWindowProcA*39
 9  CGame::InitializeGame                        2         time, srand
10  CRegKey::GetValueFromKey                    45         RegOpenKeyExA*20, RegCloseKey*20, RegQueryValueExA*5
11  CInstallInfo::SetGameHDPath                  1
12  CInstallInfo::SetGameCDPath                  1
13  CInstallInfo::LoadInstallPathsFromRegistry   3         _stricmp*3
14  CInput::DInputCreate                         1         dinput.DirectInputCreateEx
15  CInput::SetupKeyboard                        2         SystemParametersInfoA*2
16  CInput::SetupJoystick                       32         strncpy*32
17  gz_open / zcalloc / zcfree / destroy        30         zlib
21  CFileBuffer::GetGenericFileBuffer            2         GetFileAttributesA*2
23  CGraphics::InitializeDirectX                 2         ddraw.DirectDrawCreateEx, MessageBoxA
```

`CGraphics::InitializeDirectX` **succeeds**:
`DirectDrawCreateEx(NULL, &pDD, IID_IDirectDraw7, NULL)` returns `0` = `DD_OK`.

Then it stops, and this is the honest end of what is currently reachable:

```
user32.MessageBoxA(hwnd, "Setting configuration to defaults", "", 0x42000)
```

A **modal first-run dialog waiting for a click.** The 50-second run sat on it;
that is why the process stays alive now instead of exiting. Everything past this
— the D3D7 device, the renderer, `Game_DrawSceneViewport` — is still unmeasured.

## 4. What the static census missed

The runtime trace found **15 Win32 symbols that the grep never counted**, all of
them firing before a first frame:

| symbol | runtime sites | fired by |
|---|---|---|
| `ntdll.NtdllDefWindowProc_A` (=`DefWindowProcA`) | **39** | `CMain::MessageHandler` |
| `user32.RegisterWindowMessageA` | **30** | `CMain::MessageHandler` |
| `user32.RedrawWindow` | 2 | `CMain::SetGameActiveState` |
| `kernel32.FlushFileBuffers` | 3 | `CLogger::LogToFile` |
| `kernel32.GetStartupInfoA` | 1 | CRT `entry` |
| `+ 10 more` (see `static_vs_runtime.csv`) | | CRT layer |

`RegisterWindowMessageA` at 30 calls is the one to notice: it sits in
`CMain::MessageHandler`'s `default:` case, so it runs for **every unhandled
message**. A shim that returns 0 still satisfies `msg != 0 == ...` but it is on
the critical path and must not be absent.

`DefWindowProcA` at 39 calls means the WndProc falls through to the default
handler 39 times before a frame. It is not decoration.

## 5. The shim build order this buys

Not compiler-error order. Measured order:

| tier | must exist for | symbols |
|---|---|---|
| **0 CRT** | the process to start | `entry`, `_initterm`, `__set_app_type`, `__getmainargs`, `_controlfp`, `malloc/calloc/realloc/free/memmove/strncpy`, `time`, `srand`, `fopen` |
| **0 memory** | `memmove*72`, `realloc*70` — the single biggest runtime consumer | (above) + `FUN_004ce163` |
| **1 logger** | anything to be observed at all | `CreateFileA`, `WriteFile`, `FlushFileBuffers`, `lstrlenA`, `lstrcpyA`, `CloseHandle` |
| **2 registry** | **the game to not exit silently** | `RegOpenKeyExA`, `RegQueryValueExA`, `RegCloseKey` |
| **3 window** | a window to exist | `RegisterClassA`, `CreateWindowExA`, `GetSystemMetrics`, `LoadIconA`, `LoadCursorA`, `GetStockObject`, `ShowWindow`, `UpdateWindow`, `SetFocus`, `FindWindowA` |
| **4 pump** | frames to be driven | `PeekMessageA`, `GetMessageA`, `TranslateMessage`, `DispatchMessageA`, `DefWindowProcA`, `RegisterWindowMessageA`, `PostQuitMessage`, `ShowCursor`, `DrawMenuBar`, `RedrawWindow` |
| **5 input** | the boot state machine to finish | `DirectInputCreateEx`, `SystemParametersInfoA` |
| **6 video** | the first frame | `ChoosePixelFormat`, `DescribePixelFormat`, `SetPixelFormat`, `DirectDrawCreateEx`, `MessageBoxA` |
| **7 render** | the frame's contents | the 27 D3D7 device methods (`SetTextureStageState`*113, `SetRenderState`*75, …) |

Tiers 0–2 are what nothing in the current tree can do without, and **tier 2 is
the one that silently kills the game** — a broken registry shim produces a
one-second silent exit, no error, no dialog, "Program finished normally".

## 6. Caveats — read these before quoting a number

1. The full run required **injecting registry values into a COPY of the prefix**
   (`~/lena/work/CMR2/depmap/pfx-relay`, 343 MB). Miami's own
   `~/.cmr2proton/pfx` was not touched. Delete the copy to remove the effect.
2. The trace stops at the "Setting configuration to defaults" dialog. **The
   runtime surface below tier 6 is therefore unmeasured** — 375 game-image calls
   is a floor, not the whole game.
3. `ddraw`/`dsound`/`dinput` being *loaded* is not the same as being *called*.
   The trace shows Wine initialising ddraw on the game's thread (ret addresses
   in `7xxxxxxx`); only `ret=004xxxxx` counts as the game firing something.
4. Relay's default `RelayFromExclude` list is
   `winex11.drv;winemac.drv;user32;gdi32;advapi32;kernel32`. That suppresses
   calls *made from inside* those DLLs. Calls from the game *into* them are
   still logged, which is what we want — but it means this trace cannot be used
   to count Wine-internal traffic.
