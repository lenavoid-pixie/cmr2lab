#!/usr/bin/env python3
"""
depmap.py -- DEPENDENCY MAP for the CMR2 native port shim.

WHY THIS EXISTS
---------------
The port had a SURFACE CENSUS (how many sites mention X) but no DEPENDENCY GRAPH
(who calls what, in what order, and what breaks if X is absent). So the shim was
being built in COMPILER-ERROR order -- the order the compiler happened to
complain in -- instead of the order the CODE actually demands.

This tool produces the graph. Four outputs:

  1. win32_surface   -- every Win32 API used, per translation unit, with site
                        counts AND the enclosing function of every site.
  2. callgraph       -- function-level edges inside the decomp tree, so a symbol
                        can be asked "who calls you" and "what do you need".
  3. classA_types    -- the class-A "unknown type name" errors turned from a flat
                        list into a tree: which struct declares the type.
  4. criticalpath    -- the WinMain -> ... -> Game_DrawSceneViewport chain with
                        the shim pieces that sit on it, in firing order.

METHOD / HONESTY NOTES
----------------------
* Comments and string/char literals are blanked to SPACES (offsets preserved),
  so a symbol named in a comment does not count as a call site. This is the
  difference between a census and a grep.
* Call attribution uses the decomp's own `// FUNCTION: CMR2 0xADDR` markers.
  A call site is attributed to the last marker before it. Coverage is reported.
* "Undefined" means: called in the tree, defined nowhere in the tree or shim.
  Those are the real shim obligations.

READ-ONLY. Writes only into this directory.
"""

import os, re, json, sys
from collections import defaultdict, Counter

ROOT = "/home/deck/lena/.lena_cmr2/port/tree/CMR2Decomp"
PLAT = "/home/deck/lena/.lena_cmr2/port/platform"
OUT  = "/home/deck/lena/work/CMR2/depmap"

# ----------------------------------------------------------------------------
# 1. source cleaning (offset preserving)
# ----------------------------------------------------------------------------
def blank_comments_strings(s):
    """Replace comment/string/char contents with spaces, keep length + newlines."""
    out = list(s)
    i, n = 0, len(s)
    while i < n:
        c = s[i]
        if c == '/' and i + 1 < n and s[i+1] == '/':
            j = s.find('\n', i)
            if j == -1: j = n
            for k in range(i, j):
                if out[k] != '\n': out[k] = ' '
            i = j
        elif c == '/' and i + 1 < n and s[i+1] == '*':
            j = s.find('*/', i + 2)
            j = n if j == -1 else j + 2
            for k in range(i, j):
                if out[k] != '\n': out[k] = ' '
            i = j
        elif c in '"\'':
            q = c; j = i + 1
            while j < n and s[j] != q:
                if s[j] == '\\': j += 1
                j += 1
            j = min(j + 1, n)
            for k in range(i, j):
                if out[k] != '\n': out[k] = ' '
            i = j
        else:
            i += 1
    return ''.join(out)


# ----------------------------------------------------------------------------
# 2. the Win32 surface, bucketed by providing DLL family
# ----------------------------------------------------------------------------
LIBC_NAMES = set('''memcpy memset memmove strlen strcpy strncpy strcat strcmp strncmp strchr
 strrchr strstr strtok strtol strtoul strtod atoi atof sprintf snprintf vsnprintf printf fprintf fscanf
 sscanf fopen fclose fread fwrite fseek ftell fflush fgetc fputc fgets fputs malloc calloc realloc free
 qsort bsearch rand srand abs labs fabs sqrt sin cos tan atan atan2 pow exp log log10 floor ceil fmod
 assert toupper tolower isalpha isdigit isspace time clock'''.split())

NOT_CALL = {'if','for','while','switch','return','sizeof','else','do','catch',
            'defined','case','and','or','not','static_assert','assert','throw','new','delete'}

WIN32 = {}
def _add(dll, *names):
    for nm in names:
        WIN32[nm] = dll

_add('user32', 'CreateWindowExA','CreateWindowExW','CreateWindowEx','RegisterClassA','RegisterClassW',
    'RegisterClass','RegisterClassExA','DefWindowProcA','DefWindowProcW','DefWindowProc',
    'GetMessageA','GetMessageW','GetMessage','PeekMessageA','PeekMessageW','PeekMessage',
    'TranslateMessage','TranslateAcceleratorA','CreateAcceleratorTableA','LoadAcceleratorA',
    'DispatchMessageA','DispatchMessageW','DispatchMessage','PostQuitMessage','FindWindowA','FindWindowW','FindWindow',
    'SetWindowLongA','SetWindowLongW','SetWindowLong','GetWindowLongA','GetWindowLongW','GetWindowLong',
    'ShowWindow','UpdateWindow','DestroyWindow','SetForegroundWindow','AdjustWindowRect','AdjustWindowRectEx',
    'GetClientRect','GetWindowRect','InvalidateRect','BeginPaint','EndPaint','GetDC','ReleaseDC',
    'GetSystemMetrics','ShowCursor','SetCursor','LoadCursorA','LoadCursorW','LoadCursor',
    'LoadIconA','LoadIconW','LoadIcon','SetCapture','ReleaseCapture','SetFocus','GetFocus',
    'MessageBoxA','MessageBoxW','MessageBox','MessageBeep','SetWindowPos','MoveWindow','DrawMenuBar',
    'SetWindowTextA','SetWindowTextW','GetWindowTextA','GetWindowTextW','SetTimer','KillTimer',
    'GetKeyboardState','GetAsyncKeyState','GetKeyState','MapVirtualKeyA','MapVirtualKeyW',
    'keybd_event','mouse_event','GetCursorPos','SetCursorPos','ClipCursor','ClientToScreen','ScreenToClient',
    'SystemParametersInfoA','SystemParametersInfoW','SystemParametersInfo','GetDesktopWindow',
    'LoadMenuA','GetMenu','SetMenu','EnableMenuItem','CheckMenuItem','GetClientRect',
    'wsprintfA','wsprintfW','CallWindowProcA','GetParent','GetWindow','EnableWindow','IsWindow',
    'SetWindowLongPtrA','GetClassLongA','SetClassLongA','GetSysColor','FillRect','FrameRect',
    'CreateWindow','ShowWindowAsync','SetActiveWindow','GetForegroundWindow','SetWindowLongA')

_add('kernel32', 'GetModuleHandleA','GetModuleHandleW','GetModuleHandle','GetModuleFileNameA','GetModuleFileNameW','GetModuleFileName',
    'GetCommandLineA','GetCommandLineW','GetCommandLine','GetLastError','SetLastError','Sleep',
    'QueryPerformanceCounter','QueryPerformanceFrequency','GetTickCount','GetTickCount64',
    'CreateFileA','CreateFileW','CreateFile','ReadFile','WriteFile','CloseHandle','SetFilePointer',
    'DeleteFileA','DeleteFile','GetFileSize','SetEndOfFile','CreateDirectoryA','RemoveDirectoryA',
    'FindFirstFileA','FindNextFileA','FindClose','GetFileAttributesA','SetFileAttributesA',
    'GetCurrentDirectoryA','SetCurrentDirectoryA','GetFullPathNameA','GetTempPathA',
    'CreateThread','WaitForSingleObject','WaitForMultipleObjects','ReleaseMutex','CreateMutexA',
    'CreateEventA','SetEvent','ResetEvent','ExitThread','GetCurrentThreadId','GetCurrentProcess',
    'GlobalAlloc','GlobalFree','GlobalLock','GlobalUnlock','GlobalHandle','LocalAlloc','LocalFree',
    'HeapAlloc','HeapFree','GetProcessHeap','GetVersion','GetVersionExA','GetSystemInfo',
    'GetLocalTime','GetSystemTime','GetTimeZoneInformation','LoadLibraryA','LoadLibraryW','LoadLibrary',
    'GetProcAddress','FreeLibrary','MultiByteToWideChar','WideCharToMultiByte','lstrlenA','lstrcpyA',
    'lstrcatA','lstrcmpA','OutputDebugStringA','OutputDebugStringW','OutputDebugString',
    'GetStartupInfoA','SetUnhandledExceptionFilter','UnhandledExceptionFilter','RaiseException',
    'FormatMessageA','TerminateProcess','GetCurrentProcessId','IsBadReadPtr','IsBadWritePtr',
    'GetEnvironmentVariableA','SetEnvironmentVariableA','GetSystemDirectoryA','GetWindowsDirectoryA')

_add('winmm', 'timeGetTime','timeBeginPeriod','timeEndPeriod','timeGetSystemTime','timeGetDevCaps',
    'timeKillEvent','timeSetEvent','PlaySoundA','PlaySoundW','PlaySound','sndPlaySoundA',
    'mciSendStringA','mciGetErrorStringA','waveOutOpen','waveOutClose','waveOutPrepareHeader',
    'waveOutUnprepareHeader','waveOutWrite','waveOutReset','waveOutGetPosition','waveOutGetDevCaps',
    'waveOutGetNumDevs','waveOutSetVolume','waveInOpen','waveInClose','waveInPrepareHeader',
    'waveInUnprepareHeader','waveInAddBuffer','waveInStart','waveInStop','waveInReset',
    'waveInGetNumDevs','mixerOpen','mixerClose','mixerGetLineInfoA','mixerGetID','mixerGetNumDevs',
    'mmioOpenA','mmioClose','mmioRead','mmioWrite','mmioSeek','mmioDescend','mmioAscend',
    'mmioCreateChunk','mmioGetInfo','mmioSetInfo','mmioFlush','mmioRenameA',
    'acmStreamOpen','acmStreamClose','acmStreamPrepareHeader','acmStreamUnprepareHeader',
    'acmStreamConvert','acmStreamSize','acmStreamReset','acmDriverOpen','acmDriverClose',
    'acmDriverEnum','acmDriverAddA','acmFormatSuggest','acmFormatDetails','acmMetrics','acmGetVersion',
    'acmFormatTagDetails','acmFilterDetails','acmStreamMessage','acmDriverDetails',
    'midiOutOpen','midiOutClose','midiOutShortMsg','midiOutReset','midiOutGetNumDevs',
    'auxGetNumDevs','auxSetVolume','joyGetPos','joyGetPosEx','joyGetNumDevs','joyGetDevCapsA')

_add('gdi32', 'SetPixelFormat','ChoosePixelFormat','DescribePixelFormat','GetPixelFormat','SwapBuffers',
    'wglCreateContext','wglDeleteContext','wglMakeCurrent','wglGetProcAddress','wglShareLists',
    'GetDeviceCaps','SetBkMode','SetTextColor','SelectObject','CreateFontA','CreateFontIndirectA',
    'DeleteObject','CreateCompatibleDC','DeleteDC','CreateCompatibleBitmap','CreateSolidBrush',
    'CreatePen','CreateBitmap','GetStockObject','SetDIBColorTable','BitBlt','StretchBlt','StretchDIBits',
    'SetDIBitsToDevice','TextOutA','DrawTextA','GetDIBits','CreateDIBSection','RealizePalette',
    'SelectPalette','CreatePalette','GetSystemPaletteEntries','SetPaletteEntries','AnimatePalette')

_add('advapi32', 'RegOpenKeyExA','RegCreateKeyExA','RegQueryValueExA','RegSetValueExA','RegCloseKey',
    'RegDeleteKeyA','RegEnumKeyExA','RegEnumValueA','RegOpenKeyA','RegQueryValueA','RegSetValueA',
    'RegDeleteValueA','RegQueryInfoKeyA','RegFlushKey','GetUserNameA')

_add('ole32', 'CoInitialize','CoUninitialize','CoCreateInstance','CoCreateInstanceEx','CoInitializeEx',
    'CoTaskMemFree','CoTaskMemAlloc','OleInitialize','OleUninitialize','IIDFromString',
    'StringFromGUID2','IsEqualGUID','IsEqualIID','CLSIDFromString')

_add('dx7-entry', 'DirectDrawCreate','DirectDrawCreateEx','DirectDrawEnumerateA','DirectDrawEnumerateExA',
    'DirectSoundCreate','DirectSoundCreate8','DirectInputCreateA','DirectInputCreateEx',
    'DirectPlayCreate','DirectPlayLobbyCreateA','D3DXInitialize','D3DXUninitialize')

_add('shell32', 'ShellExecuteA','ShellExecuteW','ShellExecute','GetOpenFileNameA','GetSaveFileNameA',
    'SHGetPathFromIDListA','SHBrowseForFolderA')

_add('crtshim', 'vsprintf','_stricmp','stricmp','strnicmp','_strnicmp','strcmpi','_snprintf',
    '_snprintf_s','_vsnprintf','_splitpath','_makepath','_fullpath','_findfirst','_findnext','_findclose',
    '_strupr','_strlwr','_atoi64','_ltoa','_itoa','_hypot','_rotl','_rotr')

# D3D7 / DDraw / DSound / DInput COM methods (called through ->, not free functions).
D3D7_METHODS = set("""QueryInterface AddRef Release
    CreateDevice CreateSurface CreateClipper CreatePalette CreateVertexBuffer CreateIndexBuffer
    EnumDevices EnumDisplayModes EnumTextureFormats EnumZBufferFormats EnumAttachmentTypes
    GetCaps GetDeviceIdentifier GetDirect3D GetAttachedSurface GetSurfaceDesc GetDC ReleaseDC
    SetRenderState GetRenderState SetTextureStageState GetTextureStageState SetTexture GetTexture
    SetTransform GetTransform SetMaterial GetMaterial SetLight GetLight LightEnable
    SetViewport GetViewport SetClipStatus SetRenderTarget GetRenderTarget BeginScene EndScene
    Clear SetVertexShader SetStreamSource DrawPrimitive DrawPrimitiveVB DrawIndexedPrimitive
    DrawIndexedPrimitiveVB DrawIndexedPrimitiveStrided Begin/End Present
    Flip Blt BltBatch BltFast Lock Unlock LockSurface ColorFill SetColorKey GetColorKey
    GetPixelFormat SetPalette GetPalette SetEntries GetCaps GetDisplayMode GetFourCCCodes
    AddAttachedSurface DeleteAttachedSurface Restore Initialize CreateSoundBuffer
    DuplicateSoundBuffer GetCurrentPosition SetCurrentPosition Play Stop SetVolume GetVolume
    SetPan GetPan SetFrequency GetFrequency GetStatus SetFormat QuerySupport SetCooperativeLevel
    Compact GetSpeakerConfig SetSpeakerConfig GetSpeakerConfig Acquire Unacquire GetDeviceState
    GetDeviceData SetDataFormat SetProperty GetProperty SetEventNotification Run Control
    CreateEffect GetEffect CreatePlayer DestroyPlayer GetPlayerState SetPlayerData GetPlayerData
    AddPlayer GetNumberOfPlayers EnumPlayers EnumGroups EnumSessions Open Send Receive
    GetMessage2 InitializeConnection AddGroup AddShortcut GetLobbyAppData SetLobbyAppData
    GetConfiguration GetPlayerName SetPlayerName""".split())

# ----------------------------------------------------------------------------
# 3. parse the tree
# ----------------------------------------------------------------------------
FUNC_MARK = re.compile(r'//\s*FUNCTION:\s*CMR2\s+0x([0-9a-fA-F]+)')

def match_braces(s, j):
    d = 0
    k = j
    n = len(s)
    while k < n:
        if s[k] == '{': d += 1
        elif s[k] == '}':
            d -= 1
            if d == 0: return k
        k += 1
    return n - 1

def func_name_from_sig(sig):
    """Last identifier-qualified name before the opening paren."""
    m = re.findall(r'([A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_~][A-Za-z0-9_]*)*)\s*\(', sig)
    return m[-1] if m else None

def parse_tu(path):
    """Return (clean, funcs). funcs = dict(name, addr, line, start, body_start, end).

    Markers are read from the RAW text (the blanker eats `// FUNCTION:`), then
    a fallback depth-0 brace scan picks up definitions that carry no marker
    (WinMain, helpers, anything the decomp tool did not label).
    """
    raw = open(path, encoding='utf-8', errors='replace').read()
    clean = blank_comments_strings(raw)
    funcs = []
    seen = set()

    for m in FUNC_MARK.finditer(raw):
        j = clean.find('{', m.end())
        if j == -1 or j - m.end() > 600:
            continue
        name = func_name_from_sig(clean[m.end():j])
        if not name:
            continue
        end = match_braces(clean, j)
        seen.add(j)
        funcs.append(dict(name=name, addr=m.group(1), marked=True,
                          line=clean.count('\n', 0, m.start()) + 1,
                          start=m.start(), body_start=j, end=end))

    # fallback: brace at depth 0 whose preceding text ends in `sig)`  
    depth = 0
    i, n = 0, len(clean)
    while i < n:
        c = clean[i]
        if c == '{':
            if depth == 0 and i not in seen:
                back = clean[max(0, i - 700):i]
                if ')' in back and ';' not in back.split(')')[-1]:
                    k = back.rfind(')')
                    # walk back to the matching '('
                    d = 0
                    jj = k
                    while jj >= 0:
                        if back[jj] == ')': d += 1
                        elif back[jj] == '(':
                            d -= 1
                            if d == 0: break
                        jj -= 1
                    if jj > 0:
                        name = func_name_from_sig(back[:jj + 1])
                        end = match_braces(clean, i)
                        if name and name not in NOT_CALL:
                            seen.add(i)
                            funcs.append(dict(name=name, addr=None, marked=False,
                                              line=clean.count('\n', 0, i) + 1,
                                              start=max(0, i - 700), body_start=i,
                                              end=end))
            depth += 1
        elif c == '}':
            depth = max(0, depth - 1)
        i += 1

    funcs.sort(key=lambda f: f['body_start'])
    return clean, funcs, raw

def owner_of(funcs, pos):
    cur = None
    for f in funcs:
        if f['body_start'] <= pos <= f['end']:
            cur = f
        elif f['start'] > pos:
            break
    return cur

def main():
    os.makedirs(OUT, exist_ok=True)
    tus = sorted(f for f in os.listdir(ROOT)
                 if f.endswith('.cpp') and '.bak' not in f)

    parsed = {}
    defined = set()
    for fn in tus:
        clean, funcs, raw = parse_tu(os.path.join(ROOT, fn))
        parsed[fn] = (clean, funcs, raw)
        for f in funcs:
            defined.add(f['name'])
    # also treat shim-declared names as defined-by-us
    shim_declared = set()
    for d in (PLAT, os.path.join(PLAT, 'dx7')):
        if not os.path.isdir(d): continue
        for fn in os.listdir(d):
            if not fn.endswith(('.h', '.cpp')) or '.bak' in fn: continue
            s = open(os.path.join(d, fn), encoding='utf-8', errors='replace').read()
            for m in re.finditer(r'\b([A-Za-z_]\w*)\s*\([^;{)]*\)\s*;', s):
                shim_declared.add(m.group(1))

    CALL = re.compile(r'(?:(?P<recv>[A-Za-z_][A-Za-z0-9_>\.\[\]]*)\s*(?P<op>->|\.)\s*)?'
                      r'(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\(')
    surface = defaultdict(lambda: dict(sites=0, tus=Counter(),
                                       callers=Counter(), lines=[]))
    all_calls = defaultdict(Counter)     # caller -> callee (free or ->method)
    member_calls = defaultdict(int)      # method name -> raw call count
    for fn in tus:
        clean, funcs, raw = parsed[fn]
        for m in CALL.finditer(clean):
            nm = m.group('name')
            op = m.group('op')
            if nm in NOT_CALL: continue
            owner = owner_of(funcs, m.start())
            oname = owner['name'] if owner else f"<top:{fn}>"
            if op:
                member_calls[nm] += 1
                if nm in D3D7_METHODS:
                    all_calls[oname]['->' + nm] += 1
                continue
            all_calls[oname][nm] += 1
            if nm in WIN32:
                e = surface[nm]
                e['sites'] += 1
                e['tus'][fn] += 1
                e['callers'][oname] += 1
                e['lines'].append(dict(tu=fn,
                    line=clean.count('\n', 0, m.start()) + 1,
                    caller=oname,
                    addr=(owner['addr'] if owner else None)))

    win32_out = []
    for nm, e in sorted(surface.items(), key=lambda kv: (-kv[1]['sites'], kv[0])):
        win32_out.append(dict(
            symbol=nm, dll=WIN32[nm], sites=e['sites'],
            tu_count=len(e['tus']),
            tus=dict(e['tus'].most_common()),
            callers=dict(e['callers'].most_common()),
            in_shim=(nm in shim_declared),
            defined_in_tree=(nm in defined),
            lines=e['lines'],
        ))

    # ---- 2. callgraph: caller -> callee, with counts ----------------------
    cg = []
    for caller, d in sorted(all_calls.items()):
        for callee, n in sorted(d.items(), key=lambda kv: (-kv[1], kv[0])):
            if callee.startswith('->'):
                kind = 'd3d7-method'
            elif callee in WIN32:
                kind = 'win32'
            elif callee in defined:
                kind = 'local'
            else:
                kind = 'undefined'
            cg.append(dict(caller=caller, callee=callee, count=n, kind=kind))

    # ---- 3. class A: the 14 unknown type names ---------------------------
    # class A types, as reported by errcensus over sweep10full
    errlog = "/home/deck/lena/work/CMR2/sweep10full/all_errors.txt"
    classA = set()
    classB = set()
    if os.path.exists(errlog):
        t = open(errlog, encoding='utf-8', errors='replace').read()
        for m in re.finditer(r"unknown type name '([^']+)'", t):
            classA.add(m.group(1))
        for m in re.finditer(r"'(\w+)' declared as an array with a negative size", t):
            classB.add(m.group(1))

    STRUCT_DEF = re.compile(
        r'\b(?:typedef\s+)?(?:struct|class|union)\s*(?:__attribute__\s*\(\([^)]*\)\)\s*)?'
        r'([A-Za-z_]\w*)?\s*\{', re.S)

    def find_structures(src):
        """Yield (tagname, start, end) for every struct/class/union body."""
        for m in STRUCT_DEF.finditer(src):
            j = m.end() - 1
            end = match_braces(src, j)
            yield (m.group(1) or '<anonymous>', m.start(), end)

    type_out = {}
    for t in sorted(classA):
        uses = []
        seen_structs = set()
        # scan every file in tree + shim for declarations/uses
        scan = [(os.path.join(ROOT, f), f) for f in os.listdir(ROOT)
                if f.endswith(('.cpp', '.h')) and '.bak' not in f]
        for d in (PLAT, os.path.join(PLAT, 'dx7')):
            if not os.path.isdir(d): continue
            scan += [(os.path.join(d, f), 'platform/' + f) for f in os.listdir(d)
                     if f.endswith(('.h', '.cpp')) and '.bak' not in f]
        # a declaration of the type itself?
        decl_re = re.compile(r'\b' + re.escape(t) + r'\b')
        typedef_re = re.compile(r'\btypedef\b[^;\n]*\b' + re.escape(t) + r'\s*;')
        defines = []
        for path, rel in scan:
            try: s = open(path, encoding='utf-8', errors='replace').read()
            except Exception: continue
            c = blank_comments_strings(s)
            if typedef_re.search(c):
                defines.append(rel)
            for mm in decl_re.finditer(c):
                ln = c.count('\n', 0, mm.start()) + 1
                # which struct encloses this use?
                holder = None
                for tag, a, b in find_structures(c):
                    if a <= mm.start() <= b:
                        if holder is None or a > holder[1]:
                            holder = (tag, a)
                if holder:
                    key = (rel, holder[0])
                    if key not in seen_structs:
                        seen_structs.add(key)
                    uses.append(dict(file=rel, line=ln, struct=holder[0]))
                else:
                    uses.append(dict(file=rel, line=ln, struct=None))
        by_struct = defaultdict(lambda: dict(files=Counter(), sites=0))
        for u in uses:
            k = u['struct'] or '<file scope>'
            by_struct[k]['files'][u['file']] += 1
            by_struct[k]['sites'] += 1
        type_out[t] = dict(
            declared_as_typedef_in=defines,
            use_sites=len(uses),
            structs={k: dict(sites=v['sites'], files=dict(v['files']))
                     for k, v in sorted(by_struct.items(), key=lambda kv: -kv[1]['sites'])},
            uses=uses,
        )

    # ---- 4. critical path -------------------------------------------------
    # traced by hand from the decomp; the tool verifies each hop exists and
    # reports the win32 calls each hop makes.
    CHAIN = ['WinMain', 'CMain::Initialize', 'CMain::CreateGameWindow',
             'CMain::MessageHandler', 'CGame::InitializeGame',
             'CGame::RunStateRenderCallbacks', 'Game_DrawSceneViewport']
    chain_out = []
    for name in CHAIN:
        calls = all_calls.get(name, Counter())
        w = [(k, v) for k, v in calls.items() if k in WIN32]
        local = [(k, v) for k, v in calls.items() if k in defined and k != name]
        undef = [(k, v) for k, v in calls.items()
                 if not k.startswith('->') and k not in defined
                 and k not in WIN32 and k not in NOT_CALL
                 and k not in LIBC_NAMES]
        where = None
        for fn in tus:
            for f in parsed[fn][1]:
                if f['name'] == name:
                    where = dict(tu=fn, line=f['line'], addr=f['addr'])
        chain_out.append(dict(
            func=name, found=where is not None, where=where,
            win32=sorted(w, key=lambda kv: -kv[1]),
            local_calls=sorted(local, key=lambda kv: -kv[1])[:25],
            undefined_calls=sorted(undef, key=lambda kv: -kv[1])[:25],
        ))

    # ---- write -------------------------------------------------------------
    data = dict(
        generated_on='Steam Deck, Lena daemon',
        tree=ROOT,
        tu_count=len(tus),
        win32_distinct=len(win32_out),
        win32_sites=sum(e['sites'] for e in win32_out),
        functions_parsed=sum(len(parsed[f][1]) for f in tus),
        classA_types=len(classA), classB_asserts=len(classB),
        surface=win32_out,
        d3d7_methods=[dict(method=k, sites=v) for k, v in
                      sorted(member_calls.items(), key=lambda kv: (-kv[1], kv[0]))
                      if k in D3D7_METHODS],
        member_calls_all=[dict(method=k, sites=v) for k, v in
                          sorted(member_calls.items(),
                                 key=lambda kv: (-kv[1], kv[0]))[:40]],
        callgraph=cg,
        classA={k: v for k, v in type_out.items()},
        critical_path=chain_out,
    )
    json.dump(data, open(os.path.join(OUT, 'depmap.json'), 'w'), indent=1)

    print(f"tree              : {ROOT}")
    print(f"TUs parsed        : {len(tus)}")
    print(f"functions parsed  : {data['functions_parsed']}")
    print(f"distinct Win32    : {data['win32_distinct']}")
    print(f"Win32 call sites  : {data['win32_sites']}")
    print(f"class A types     : {len(classA)}  ({', '.join(sorted(classA))})")
    print(f"class B asserts   : {len(classB)}")
    print()
    print("=== WIN32 SURFACE, by DLL family ===")
    fam = defaultdict(lambda: [0, 0])
    for e in win32_out:
        fam[e['dll']][0] += 1
        fam[e['dll']][1] += e['sites']
    for k in sorted(fam, key=lambda k: -fam[k][1]):
        print(f"  {k:10s} {fam[k][0]:3d} distinct  {fam[k][1]:4d} sites")
    print()
    print("=== TOP 25 WIN32 SITES (symbol / sites / TUs / defined?) ===")
    for e in win32_out[:25]:
        d = ('shim' if e['in_shim'] else 'UNRESOLVED')
        print(f"  {e['sites']:3d}  {e['symbol']:28s} {e['tu_count']:2d} TUs  [{e['dll']}] {d}")
    print()
    d3d = data['d3d7_methods']
    print(f"=== D3D7/COM METHOD SURFACE: {len(d3d)} distinct, "
          f"{sum(d['sites'] for d in d3d)} sites ===")
    for e in d3d[:18]:
        print(f"  {e['sites']:3d}  ->{e['method']}")
    print()
    print("=== CRITICAL PATH ===")
    for c in chain_out:
        loc = f"{c['where']['tu']}:{c['where']['line']}" if c['where'] else 'NOT FOUND'
        print(f"  {c['func']:32s} {loc}")
        if c['win32']:
            print(f"      win32: {', '.join(f'{k}*{v}' for k, v in c['win32'])}")
        if c['undefined_calls']:
            print(f"      undefined: {', '.join(k for k, v in c['undefined_calls'][:10])}")

if __name__ == '__main__':
    main()
