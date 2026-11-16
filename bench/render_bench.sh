#!/usr/bin/env bash
# Measures the real ./terminal window's rendering cost:
#   - idle CPU (cursor blink redraws), for the terminal and for Xorg
#   - how long it takes to stream N lines of output to the screen
#     (`seq 1 N`), with the CPU time spent by the terminal and by Xorg
#
# Needs a working X display, wmctrl, and bench/xkeys (built by run_all.sh).
# The terminal runs in a temporary folder, so the typed commands go to a
# throwaway history file, not the project's .terminal.history.txt.

set -u
cd "$(dirname "$0")/.."
ROOT=$(pwd)

if [ -z "${DISPLAY:-}" ]; then
    echo "No \$DISPLAY set — skipping render benchmark." >&2
    exit 1
fi
for f in ./terminal ./function ./bench/xkeys; do
    if [ ! -x "$f" ]; then
        echo "$f not found — run 'bash bench/run_all.sh' (or make + build bench/xkeys) first." >&2
        exit 1
    fi
done

HZ=$(getconf CLK_TCK)
XORG=$(pgrep -x Xorg | head -1)
ticks() { awk '{print $14 + $15}' "/proc/$1/stat" 2>/dev/null || echo 0; }
ms() { echo $(( $1 * 1000 / HZ )); }

RUN_DIR=$(mktemp -d)
ln -s "$ROOT/terminal" "$RUN_DIR/terminal"
ln -s "$ROOT/function" "$RUN_DIR/function"

(cd "$RUN_DIR" && exec ./terminal >/dev/null 2>&1) &
TP=$!
cleanup() {
    kill "$TP" >/dev/null 2>&1
    wait "$TP" 2>/dev/null
    rm -rf "$RUN_DIR"
}
trap cleanup EXIT

WID=""
for _ in $(seq 1 100); do
    WID=$(wmctrl -l 2>/dev/null | grep "XTerm \[Ctrl-T new tab" | awk '{print $1}' | head -1)
    [ -n "$WID" ] && break
    sleep 0.05
done
if [ -z "$WID" ]; then
    echo "terminal window did not appear" >&2
    exit 1
fi

echo "XTerm render benchmark"
echo "---------------------------------------------------"

# Idle: wait past the 5 s "recently typed" window so the cursor blinks normally.
sleep 6
IDLE_S=10
t0=$(ticks "$TP"); x0=$(ticks "$XORG")
sleep "$IDLE_S"
t1=$(ticks "$TP"); x1=$(ticks "$XORG")
printf "idle (%d s)          : terminal %5.2f%% CPU   Xorg %5.2f%% CPU (Xorg includes all other apps)\n" \
    "$IDLE_S" "$(echo "scale=4; ($t1 - $t0) * 100 / $HZ / $IDLE_S" | bc)" \
    "$(echo "scale=4; ($x1 - $x0) * 100 / $HZ / $IDLE_S" | bc)"
printf "RSS                  : %s KB\n" "$(ps -o rss= -p "$TP" | tr -d ' ')"

# Output burst: type `seq 1 N`, then wait until the terminal's CPU time stops
# rising for 3 samples in a row (0.3 s), which means the output is drawn.
for N in 20000 100000; do
    t0=$(ticks "$TP"); x0=$(ticks "$XORG")
    ./bench/xkeys "$WID" type "seq 1 $N"
    s=$(date +%s%N)
    ./bench/xkeys "$WID" key Return

    prev=-1; still=0
    while [ "$still" -lt 3 ]; do
        sleep 0.1
        c=$(ticks "$TP")
        if [ "$c" = "$prev" ]; then still=$((still + 1)); else still=0; prev=$c; fi
    done
    e=$(date +%s%N)
    t1=$(ticks "$TP"); x1=$(ticks "$XORG")

    wall=$(( (e - s) / 1000000 - 300 ))
    printf "seq 1 %-7d        : %5d ms on screen   terminal CPU %4d ms   Xorg CPU %4d ms\n" \
        "$N" "$wall" "$(ms $((t1 - t0)))" "$(ms $((x1 - x0)))"
    ./bench/xkeys "$WID" type "clear"
    ./bench/xkeys "$WID" key Return
    sleep 0.5
done

echo "RSS after output     : $(ps -o rss= -p "$TP" | tr -d ' ') KB"
