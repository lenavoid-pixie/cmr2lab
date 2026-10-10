#!/bin/bash
# relay-run.sh -- run the ORIGINAL CMR2.exe under Wine with WINEDEBUG=+relay and
# capture the runtime Win32 call surface. Wine is the reference implementation of
# the exact layer the port is converting FROM, so this is the layer MEASURED
# rather than counted from a grep.
#
# Usage: relay-run.sh SECONDS OUTPREFIX
#   -> OUTPREFIX.raw   (full stderr)
#   -> OUTPREFIX.relay (filtered, ordered call stream)
set -u
SECS=${1:-25}
OUTPFX=${2:-/home/deck/lena/work/CMR2/depmap/relay}
PROTON=$HOME/.steam/root/compatibilitytools.d/GE-Proton11-7

# inherit the live desktop session (an ssh shell has no WAYLAND_DISPLAY)
PS=$(pgrep -u "$(id -u)" -x plasmashell | head -1)
if [ -n "$PS" ] && [ -r "/proc/$PS/environ" ]; then
  while IFS= read -r line; do
    case "$line" in
      WAYLAND_DISPLAY=*|DISPLAY=*|DBUS_SESSION_BUS_ADDRESS=*|XDG_RUNTIME_DIR=*)
        export "$line";;
    esac
  done < <(tr '\0' '\n' < "/proc/$PS/environ")
fi

export WINEPREFIX=$HOME/.cmr2proton/pfx
export WINEDEBUG=+relay
# keep the noise floor down; relay output is stderr
unset PROTON_LOG

cd "$HOME/lena/.lena_cmr2/game" || exit 1

timeout "$SECS" "$PROTON/files/bin/wine" CMR2.exe > /dev/null 2> "$OUTPFX.raw" &
WPID=$!
# wine's own process tree needs the timeout to reach it
wait $WPID
rc=$?
"$PROTON/files/bin/wineserver" -k >/dev/null 2>&1

python3 /home/deck/lena/work/CMR2/depmap/relay_filter.py "$OUTPFX.relay" < "$OUTPFX.raw"
echo "relay-run: wine rc=$rc"
wc -l "$OUTPFX.raw" "$OUTPFX.relay"
