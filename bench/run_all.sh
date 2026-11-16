#!/usr/bin/env bash
# Builds and runs all XTerm benchmarks.
set -e
cd "$(dirname "$0")/.."

echo "== Building project (function, terminal) =="
make

echo
echo "== Building benchmarks =="
g++ -std=c++17 -O2 -Wall bench/ansi_bench.cpp -o bench/ansi_bench
g++ -std=c++17 -O2 -Wall bench/shell_bench.cpp -o bench/shell_bench
g++ -std=c++17 -O2 -Wall bench/xkeys.cpp -o bench/xkeys -lX11

echo
echo "== [1/4] ANSI/SGR parser microbenchmark =="
./bench/ansi_bench

echo
echo "== [2/4] Shell backend (protocol checks + dispatch latency + bulk throughput) =="
./bench/shell_bench

echo
echo "== [3/4] GUI startup + memory footprint =="
bash bench/startup_mem.sh || echo "(skipped)"

echo
echo "== [4/4] Rendering: idle CPU + output to screen =="
bash bench/render_bench.sh || echo "(skipped)"
