#!/bin/bash
# Run the game with --d3d9 dxvk on a private Xvfb screen and click points of
# its picture, for checks of the menus without a desktop (docs/BUILD.md 6).
#
#   tools/xvfb_clicks.sh HOST GAME OUT WAIT X,Y[:PAUSE] ...
#
# HOST is tm_host, GAME the game directory, OUT a new directory for the
# run's log (out.txt), screens (click_N.png, end.png) and frames
# (TM_D3D9_SHOTS). WAIT is the seconds before the first click. Each X,Y is a
# point of the game's 1024x768 picture; PAUSE the seconds after that click
# (default 10).
#
# The game moves its own cursor by DirectInput's relative movement, scaled
# by its mouse sensitivity (SENS, 1.5 measured with the default settings),
# and centres the system cursor each frame. So each click first moves the
# cursor into the top-left corner, then by (X,Y)/SENS. The window is
# win32hle's 1280x960 (SCALE 1.25 of the picture); the screen is made the
# same size, and the run uses --mouse free (Xvfb's pointer barriers stop
# win32hle's cursor warps).
set -e
HOST=$1 GAME=$2 OUT=$3 WAIT=$4; shift 4
SENS=${SENS:-1.5} DISP=${DISP:-:97}
mkdir -p "$OUT/shots"
Xvfb $DISP -screen 0 1280x960x24 >/dev/null 2>&1 & XP=$!
trap 'kill $GP $XP 2>/dev/null' EXIT
sleep 2; export DISPLAY=$DISP
(cd "$GAME" && TM_D3D9_SHOTS="$OUT/shots" exec "$HOST" . --user "$OUT/user" --system "$OUT/sys" \
    --watchdog ${WATCHDOG:-600} --d3d9 dxvk --mouse free) > "$OUT/out.txt" 2>&1 & GP=$!
sleep "$WAIT"
n=0
for c in "$@"; do
    n=$((n + 1)); xy=${c%%:*}; pause=10; [ "$c" != "$xy" ] && pause=${c#*:}
    x=${xy%,*} y=${xy#*,}
    kill -0 $GP 2>/dev/null || { echo "the game ended before click $n"; break; }
    for i in $(seq 30); do xdotool mousemove_relative -- -60 -60; sleep 0.03; done
    dx=$(python3 -c "print(round($x / $SENS * 1.25 / 10))"); dy=$(python3 -c "print(round($y / $SENS * 1.25 / 10))")
    for i in $(seq 10); do xdotool mousemove_relative -- $dx $dy; sleep 0.03; done
    sleep 1; import -window root "$OUT/aim_$n.png"
    xdotool mousedown 1; sleep 0.2; xdotool mouseup 1
    sleep "$pause"; import -window root "$OUT/click_$n.png"
done
import -window root "$OUT/end.png" || true
grep -E "FATAL|=== abort|in lifted" "$OUT/out.txt" || echo "no stop: the run went on"
