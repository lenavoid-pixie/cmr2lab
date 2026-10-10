#!/bin/bash
# relay-run2.sh -- run CMR2.exe with +relay AND auto-dismiss the game's own modal
# dialogs so the trace continues past the first-run MessageBox into the real
# frontend / renderer.
#
# SAFETY: keys are only ever sent to windows owned by the wine process tree.
# Nothing is sent to Miami's own windows, ever.
#
# Usage: relay-run2.sh SECONDS OUTRAW
set -u
SECS=${1:-45}
OUT=${2:-/tmp/relay5.raw}
PREFIX=$HOME/lena/work/CMR2/depmap/pfx-relay
PROTON=$HOME/.steam/root/compatibilitytools.d/GE-Proton11-7
WINE=$PROTON/files/bin/wine

PS=$(pgrep -u "$(id -u)" -x plasmashell | head -1)
if [ -n "$PS" ] && [ -r "/proc/$PS/environ" ]; then
  while IFS= read -r line; do
    case "$line" in
      WAYLAND_DISPLAY=*|DISPLAY=*|DBUS_SESSION_BUS_ADDRESS=*|XDG_RUNTIME_DIR=*)
        export "$line";;
    esac
  done < <(tr '\0' '\n' < "/proc/$PS/environ")
fi

export WINEPREFIX=$PREFIX
export WINEDEBUG=+relay
cd "$HOME/lena/.lena_cmr2/game" || exit 1

timeout "$SECS" "$WINE" CMR2.exe > /dev/null 2> "$OUT" &
WPID=$!

# dismiss only windows belonging to the wine process tree
DIS=0
END=$(( $(date +%s) + SECS - 2 ))
while [ "$(date +%s)" -lt "$END" ]; do
  for w in $(xdotool search --pid "$WPID" 2>/dev/null); do
    # confirm the window's pid is still inside the wine tree
    WP=$(xdotool getwindowpid "$w" 2>/dev/null) || continue
    [ -z "$WP" ] && continue
    if [ "$WP" = "$WPID" ] || [ "$(ps -o ppid= -p "$WP" 2>/dev/null | tr -d ' ')" = "$WPID" ]; then
      xdotool windowactivate --sync "$w" 2>/dev/null
      xdotool key --window "$w" --clearmodifiers Return 2>/dev/null
      xdotool key --window "$w" --clearmodifiers space 2>/dev/null
      DIS=$((DIS+1))
    fi
  done
  sleep 1
done

wait $WPID
rc=$?
"$PROTON/files/bin/wineserver" -k >/dev/null 2>&1
echo "relay-run2: rc=$rc dismissals=$DIS raw=$(stat -c%s "$OUT" 2>/dev/null)"
