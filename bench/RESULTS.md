# XTerm Benchmark Results

Captured by running `bash bench/run_all.sh` on the development machine. See [`README.md`](README.md) for what each benchmark measures and how to reproduce these numbers on your own hardware.

**Environment**

|           |                                     |
| --------- | ----------------------------------- |
| Date      | 2026-11-16                          |
| CPU cores | 8                                   |
| Display   | local X11 session (`$DISPLAY=:0`) |
| Compiler  | g++,`-std=c++17 -O2`              |
| Version   | after the UI update: themes, selection/clipboard, continuation prompt |

## 1. ANSI/SGR parser throughput

`headers/ansi_core.h::AnsiToSegments` pure CPU, no X11/PTY. The payload now also contains the private prompt color codes (`ESC[200m`–`ESC[203m`).

| Payload | Iterations | Time    | Throughput | Calls/s |
| ------- | ---------- | ------- | ---------- | ------- |
| 1 KB    | 20,000     | 0.263 s | 74.60 MB/s | 75,941  |
| 64 KB   | 2,000      | 2.792 s | 44.77 MB/s | 716     |
| 1 MB    | 200        | 5.422 s | 36.89 MB/s | 37      |
| 16 MB   | 15         | 5.813 s | 41.29 MB/s | 3       |
| Prompt line (78 B, parsed every redraw) | 200,000 | 0.227 s | 1,136 ns/call | 880,447 |

**Headline: ~37-75 MB/s escape-sequence parsing, up to ~76k calls/s on small payloads; the prompt costs ~1.1 µs per redraw.**

## 2. Shell backend (`./function`)

Driven headlessly via the same fork+pipe+`execvp` path `Create_Tab()` uses.

**Protocol checks: 5/5 pass**

| Check | Result |
| ----- | ------ |
| Built-in `echo` prints after an external command | PASS |
| Unclosed quote sends continuation start/end markers | PASS |
| Continued quote prints both lines | PASS |
| Ctrl+C cancel ends the continuation | PASS |
| Cancelled `echo` prints nothing | PASS |

| Test                           | Result                                                           |
| ------------------------------ | ---------------------------------------------------------------- |
| Command dispatch latency       | 200 commands in 20.207 s →**9.90 cmds/s** (101.04 ms/cmd) |
| Bulk throughput, 64 MB`cat`  | 0.201 s → 429.43 MB/s                                           |
| Bulk throughput, 256 MB`cat` | 0.702 s → 492.88 MB/s                                           |
| Bulk throughput, 512 MB`cat` | 1.403 s → 493.04 MB/s                                           |

**Headline: ~490 MB/s sustained bulk throughput once payloads are large enough to amortize fixed overhead.**

Dispatch latency is flat at ~100ms/command bound by the `poll(&pfd, 1, 100)` timeout in `function.cpp`'s main loop, not by actual command execution cost.

## 3. GUI startup + memory footprint

`./terminal`, window-mapped detection via `wmctrl`, 5 runs.

| Run           | Startup time      | RSS                            |
| ------------- | ----------------- | ------------------------------ |
| 1             | 57.0 ms           | 23,580 KB                      |
| 2             | 29.3 ms           | 23,636 KB                      |
| 3             | 21.4 ms           | 23,732 KB                      |
| 4             | 96.3 ms           | 23,768 KB                      |
| 5             | 76.5 ms           | 23,632 KB                      |
| **avg** | **56.1 ms** | **23,669 KB (~23.1 MB)** |

## 4. Rendering

`./terminal` in a temporary folder, commands typed with `bench/xkeys`. Timings are accurate to about ±100 ms (see the README notes).

| Test | On screen | Terminal CPU | Xorg CPU |
| ---- | --------- | ------------ | -------- |
| Idle, 10 s (cursor blink) | — | 0.20% | 10.9% (includes all other apps) |
| `seq 1 20000` | 358 ms | 10 ms | 60 ms |
| `seq 1 100000` | 362 ms | 20 ms | 70 ms |

RSS stays flat: 23,700 KB before the output and 23,828 KB after 120,000 lines (the scrollback keeps at most 4,000 lines).

**Headline: 100,000 lines reach the screen in ~0.36 s using ~20 ms of terminal CPU, and the window idles at ~0.2% CPU.**

## Compared with the previous run (2026-08-19)

| Metric | Before | Now |
| ------ | ------ | --- |
| ANSI parser, 1 KB | 77.90 MB/s | 74.60 MB/s (payload now includes prompt codes) |
| ANSI parser, 16 MB | 42.60 MB/s | 41.29 MB/s |
| Bulk throughput, 512 MB | 492.12 MB/s | 493.04 MB/s |
| Dispatch latency | 100.97 ms/cmd | 101.04 ms/cmd |
| Startup | 62.6 ms | 56.1 ms |
| RSS after startup | 23,192 KB | 23,669 KB (+0.5 MB for themes and selection) |

Everything is within run-to-run noise except a ~0.5 MB larger memory footprint.

## Resume-ready summary

> Built a custom X11 terminal emulator in C++ (Xlib/Xft, custom VT/ANSI parser, PTY-free shell backend) with 19 switchable color themes, mouse selection with clipboard integration, and a bash-style continuation prompt. Benchmarked the ANSI escape-sequence parser at ~40 MB/s / ~76k calls/s, the shell I/O backend at ~490 MB/s sustained throughput, 100,000 lines rendered in ~0.36 s, and cold-start latency at ~55 ms to first window with a ~23 MB memory footprint and ~0.2% idle CPU.

Numbers are hardware-dependent re-run `bash bench/run_all.sh` before reusing them and update this file with your own results.
