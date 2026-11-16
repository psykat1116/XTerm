# XTerm Benchmarks

Four self-contained benchmarks that measure real numbers from this codebase, useful for backing up a resume bullet with actual data instead of a guess. All of them exercise production code paths, not reimplementations.

Run everything with:

```bash
bash bench/run_all.sh
```

This builds the project (`make`) and the benchmark binaries, then runs the four steps below. Steps 3 and 4 open real windows on your screen for a few seconds each.

## What each one measures

**`[ansi_bench]`** pure-CPU microbenchmark of the production escape-sequence parser (`headers/ansi_core.h::AnsiToSegments`). Feeds it synthetic ANSI-colored CLI-style output at several sizes (including the private `ESC[200m`–`ESC[203m` prompt color codes) and reports MB/s and calls/s. Also times parsing the prompt line itself, which `Draw_UI()` does on every redraw. No X11 or PTY involved, so it isolates the parser itself.

**`[shell_bench]`** spawns `./function` exactly the way `terminal.cpp`'s `Create_Tab()` does (fork + pipe + `execvp`), so it drives the real command dispatcher and I/O path, minus only the final Xft draw call. It first runs **protocol checks** (PASS/FAIL) for the behavior the UI depends on, then reports:

- protocol checks:
  - built-in `echo` still prints after an external command has written to stdout
  - an unclosed quote (`echo "...`) sends the continuation markers (`0x0e` / `0x0f`) and the next line completes it
  - the Ctrl+C cancel line (`0x03`) ends a continuation without printing anything
- command dispatch latency (round-trip time from writing a command line to seeing the backend's completion marker)
- bulk throughput of streaming large command output back through the pipe (`cat` of a generated multi-hundred-MB file)

It exits with status 1 if any protocol check fails.

**`[startup_mem.sh]`** launches the real `./terminal` GUI binary, uses `wmctrl` to detect when the X11 window is mapped, and reads `RSS` from `/proc`/`ps`. Needs a working `$DISPLAY`; skips itself under a headless environment. Averages over 5 runs.

**`[render_bench.sh]`** measures the real window's drawing cost:

- **idle CPU** over 10 s (cursor blink redraws), for the terminal and for Xorg
- **output to screen:** types `seq 1 20000` and `seq 1 100000` into the window (via the small `bench/xkeys` helper, which sends synthetic X11 key events) and reports how long until the output stops changing, plus the CPU time the terminal and Xorg spent on it

It runs the terminal in a temporary folder, so the typed commands go to a throwaway history file, not the project's `.terminal.history.txt`.

## Example numbers (8-core laptop, local X session)

```
ANSI parser:        ~37-75 MB/s, up to ~76k calls/s on small payloads
                    ~1.1 µs to parse the prompt line on each redraw
Shell backend:      ~490 MB/s sustained bulk throughput
                    ~10 commands/s dispatch rate (~100 ms/cmd, bound by function.cpp's 100ms poll loop — see notes below)
                    5/5 protocol checks pass
GUI startup:        ~55 ms to first mapped window
Idle memory (RSS):  ~23 MB per window
Idle CPU:           ~0.2% (cursor blink)
Output to screen:   100,000 lines in ~0.36 s, ~20 ms terminal CPU
```

Exact numbers vary by machine re-run `bench/run_all.sh` and use your own.

## Notes / gotchas if you extend these

- Small `cat` payloads (a few MB) in `shell_bench` are dominated by `function.cpp`'s fixed ~100ms child-exit poll interval, not real transfer speed that's why the bulk-throughput cases use 64/256/512 MB payloads (multi-second transfers where the fixed overhead is negligible).
- The `echo` and `cd` builtins never emit the `0x02` completion marker (only external/forked commands do), so `Run_Command_And_Wait` in `shell_bench.cpp` uses `true` for the latency test, and each protocol check ends with `true` so its marker shows that all earlier output has arrived.
- The protocol markers are defined once in `headers/util.h` (`CONTINUATION_START`, `CONTINUATION_END`, `CANCEL_LINE`). `shell_bench.cpp` uses the raw byte values; keep them in sync if those change.
- `render_bench.sh` detects "done" by waiting until the terminal's CPU time stops rising for 0.3 s, sampling every 0.1 s. Its timings are only accurate to about ±100 ms and include the shell's ~100 ms dispatch poll, so most of the ~0.36 s is fixed overhead. The Xorg CPU figures also include whatever other apps are drawing at the same time.
- `headers/tab.h` expects the includer to already have `<csignal>`, `<fcntl.h>`, and `<unistd.h>` pulled in (as `terminal.cpp`/`function.cpp` do); standalone bench files include them explicitly before `ansi_core.h`.
