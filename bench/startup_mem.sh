#!/usr/bin/env bash
# Measures GUI process startup time and memory footprint of the real
# ./terminal binary (X11 window, Xft font rendering, tab creation).
# Requires a working X display ($DISPLAY) — run this on your desktop,
# not inside a headless CI/container.

set -u
cd "$(dirname "$0")/.."

if [ -z "${DISPLAY:-}" ]; then
    echo "No \$DISPLAY set — skipping GUI startup/memory benchmark." >&2
    exit 1
fi
if [ ! -x ./terminal ]; then
    echo "./terminal not found — run 'make' first." >&2
    exit 1
fi

RUNS=5
echo "XTerm GUI startup + memory benchmark ($RUNS runs)"
echo "---------------------------------------------------"

total_startup=0
total_rss=0

for i in $(seq 1 "$RUNS"); do
    t0=$(date +%s.%N)
    ./terminal >/tmp/xterm_bench_stdout.log 2>&1 &
    pid=$!

    # Poll until the window is mapped (visible) via wmctrl. The app doesn't
    # set _NET_WM_PID, so match on its fixed window title instead.
    mapped=0
    for _ in $(seq 1 100); do
        if wmctrl -l 2>/dev/null | grep -q "XTerm \[Ctrl-T new tab"; then
            mapped=1
            break
        fi
        sleep 0.02
    done
    t1=$(date +%s.%N)

    if [ "$mapped" -eq 1 ]; then
        startup_ms=$(printf "%.1f" "$(echo "($t1 - $t0) * 1000" | bc)")
    else
        startup_ms="timeout"
    fi

    sleep 0.3 # let the tab/child processes settle
    rss_kb=$(ps -o rss= -p "$pid" 2>/dev/null | tr -d ' ')
    rss_kb=${rss_kb:-0}

    echo "run $i: startup=${startup_ms} ms   RSS=${rss_kb} KB"

    if [ "$mapped" -eq 1 ]; then
        total_startup=$(echo "$total_startup + $startup_ms" | bc)
    fi
    total_rss=$((total_rss + rss_kb))

    kill "$pid" >/dev/null 2>&1
    wait "$pid" 2>/dev/null
done

echo "---------------------------------------------------"
echo "avg startup: $(echo "scale=1; $total_startup / $RUNS" | bc) ms"
echo "avg RSS after startup: $((total_rss / RUNS)) KB"
