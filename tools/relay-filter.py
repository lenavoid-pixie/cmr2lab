#!/usr/bin/env python3
"""
relay_filter.py -- live filter for a WINEDEBUG=+relay stream.

Reads wine's stderr on stdin, writes the parts that matter to the file given
as argv[1]. Keeps:

  * `Call PE DLL (... module=... L"x.dll",reason=PROCESS_ATTACH ...)`  -> DLL
    load ORDER, which is the loader-level dependency sequence.
  * `tid:Call <module>.<func>(...)` for the Win32 shim DLLs only.
  * `tid:Ret  <module>.<func>(...)` for the same, so call duration/return can
    be paired.

Drops ntdll/kernelbase internal chatter, which is ~90% of the volume and none
of the information. Counts everything it dropped so the run is auditable.

Usage: wine ... 2>&1 >/dev/null | python3 relay_filter.py OUT [MAXLINES]
"""
import sys, re, collections

out_path = sys.argv[1]
max_lines = int(sys.argv[2]) if len(sys.argv) > 2 else 4_000_000

KEEP = {
    'user32', 'kernel32', 'gdi32', 'advapi32', 'winmm', 'mmsystem',
    'ole32', 'oleaut32', 'comdlg32', 'shell32', 'version', 'winspool',
    # graphics / audio / input / net shim targets
    'ddraw', 'd3d', 'd3dim', 'd3dxof', 'd3drm', 'dsound', 'dinput',
    'dplayx', 'dplay', 'dplobby', 'binkw32', 'msacm32', 'msvcrt',
    'msvcrt40', 'msacm32.drv', 'winmmbase',
}

call_re = re.compile(r'^([0-9a-f]{4}):(Call|Ret)\s+([A-Za-z0-9_.\-]+)\.([A-Za-z0-9_]+)')
dll_re = re.compile(r'Call PE DLL \(.*module=.*L"([^"]+)".*reason=(\w+)')

seen = collections.Counter()
kept = 0
total = 0
with open(out_path, 'w', buffering=1 << 20) as out:
    for line in sys.stdin:
        total += 1
        m = dll_re.search(line)
        if m:
            out.write(f"#DLL {m.group(2)} {m.group(1)}\n")
            seen['DLL'] += 1
            continue
        m = call_re.match(line)
        if not m:
            continue
        mod = m.group(3).lower()
        mod = mod.split('.')[0]
        if mod not in KEEP:
            seen['dropped:' + mod] += 1
            continue
        out.write(line)
        kept += 1
        seen['kept'] += 1
        if kept >= max_lines:
            out.write('#CAP max_lines reached\n')
            break

sys.stderr.write(f"relay_filter: total={total} kept={kept}\n")
sys.stderr.write("relay_filter: top dropped modules: " +
                 ', '.join(f"{k}={v}" for k, v in seen.most_common(12)) + "\n")
