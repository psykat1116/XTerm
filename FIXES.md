# 📄 Fixes & Improvements Log

This file records notable problems found in this project and the solutions applied, for future reference. Each entry follows a **Problem → Solution** format. See [DESIGNDOC.md](DESIGNDOC.md) for the overall architecture writeup.

---

## 1. Build process had no Makefile

**Problem:** The only documented way to build was two manual `g++` invocations copied from the README, one per binary (`function`, `terminal`), with no single command to build both, rebuild on change, or clean up artifacts.

**Solution:** Added a [Makefile](Makefile) with:

- `make` / `make all`  builds both `function` and `terminal` using the exact flags from the README (`-lX11 -lXft -I/usr/include/freetype2`).
- `make run`  builds and launches `terminal`.
- `make clean`  removes both binaries.

Updated [README.md](README.md) to point at `make` instead of the raw compiler commands.

---

## 2. `multiWatch` Ctrl-Z (SIGTSTP) bugs in `function.cpp`

A `/code-review` pass on the `multiWatch` Ctrl-Z handling in [function.cpp](function.cpp) turned up five related bugs, all now fixed.

### 2.1 Stopped child processes leaked permanently

**Problem:** When a `multiWatch` child was stopped via Ctrl-Z, its pid was erased from `g_child_pid`  unlike the plain-command path, which deliberately *keeps* a stopped pid in `g_child_pid` so it stays signalable. Since the shell has no `fg`/`bg` job-control command anywhere, once a `multiWatch` child's pid dropped out of `g_child_pid`, nothing in the codebase could ever signal it again (`Handle_Signal`'s SIGINT/SIGTERM path only walks `g_child_pid`). The stopped process leaked as a permanent orphan for the rest of the shell session.

**Solution:** The `WIFSTOPPED` branch of the reap loop no longer erases the pid from `g_child_pid`, matching the plain-command path. The stopped child stays trackable and killable via the existing SIGINT/SIGTERM handling.

### 2.2 Signal handler raced with STL containers being mutated by the main thread

**Problem:** `Handle_Signal` iterated (and, in the SIGINT/SIGTERM branch, erased from) `WProcess` and `g_child_pid`  both `std::vector`s containing `std::string` members  while the main thread was concurrently running `push_back`/`erase` on the same containers during the `multiWatch` fork and reap loops. Neither side is async-signal-safe; a SIGTSTP/SIGINT landing mid-`push_back` (possibly reallocating) or mid-`erase` (shifting/destructing elements) could corrupt the heap.

**Solution:** Added `Block_Watch_Signals()`/`Restore_Watch_Signals()` helpers (`sigprocmask` blocking `SIGINT`+`SIGTSTP`) and wrapped every `WProcess`/`g_child_pid` mutation point in the `multiWatch` path with them, so `Handle_Signal` can never run while a mutation is half-done.

### 2.3 Tempfile deleted while the (still-alive) child might still be writing to it

**Problem:** On Ctrl-Z, the tempfile backing a `multiWatch` child's output was `remove()`d and its fd closed as soon as the process was merely *stopped*  not exited. Combined with bug 2.1, the process stayed alive (just paused) with no way to resume it, so any output it produced after being stopped had nowhere to go, and the file was deleted while still nominally "owned" by a live process.

**Solution:** The `WIFSTOPPED` branch no longer closes the fd or removes the tempfile  only actual exit (normal or error) does that cleanup now.

### 2.4 Redundant signal loop

**Problem:** A newly-added loop in `Handle_Signal`'s SIGTSTP branch sent `SIGTSTP` to every pid in `WProcess`, but every `WProcess` pid is also pushed into `g_child_pid` at fork time  so the pre-existing `g_child_pid` loop right above it already signaled the same process groups. Every Ctrl-Z sent `SIGTSTP` to each `multiWatch` child twice.

**Solution:** Removed the redundant `WProcess` loop; the `g_child_pid` loop covers it.

### 2.5 `multiWatch` children didn't reset `SIGTSTP` before `exec`

**Problem:** The pipe path and the plain-command path both call `signal(SIGTSTP, SIG_DFL)` right after `fork()`, before `execvp`, so a stray SIGTSTP delivered in that narrow window just stops the child instead of re-entering the parent's signal handler inside the child process. The `multiWatch` fork's child branch was missing this reset.

**Solution:** Added `signal(SIGTSTP, SIG_DFL)` to the `multiWatch` child branch, matching the other two paths.

---

## 3. Custom terminal couldn't render color or Unicode

### 3.1 ANSI color codes weren't interpreted

**Problem:** `terminal.cpp` connects the shell (and the shell's own children  `ls`, `grep`, `git`, ...) over plain `pipe()`s, never a pseudo-terminal. Two compounding issues:

1. Most CLI tools check `isatty(stdout)` before emitting color and silently disable it when talking to a pipe, so color often never gets sent in the first place.
2. Even when color codes *did* arrive (e.g. `ls --color=always`), `Add_To_Buffer` stored the raw bytes as part of a plain `string` line and `DrawUtf8` drew them with a single hardcoded white `XftColor`  there was no ANSI/SGR parsing anywhere, so escape bytes just rendered as garbage instead of switching color.

**Solution (deliberately not using `forkpty()`/`openpty()`**  see note below): wrote a byte-level ANSI SGR parser by hand in `Add_To_Buffer` ([terminal.cpp](terminal.cpp)):

- Walks child output byte-by-byte, recognizing `ESC [ params m` (SGR/color codes) and applying them to running per-tab state (`current_color`, `current_bold`).
- Recognizes but safely discards other CSI sequences (cursor movement, erase, etc.) and OSC sequences (window title, ...) that this terminal doesn't implement, so they don't corrupt visible text.
- Text between escape sequences is grouped into colored `Segment{text, color}` runs; `Tab::buffer` changed from `vector<string>` to `vector<vector<Segment>>` to hold multiple colors per line ([headers/tab.h](headers/tab.h)).
- Parser state (`raw_pending`, `current_color`, `current_bold`, `current_line`) persists across separate `read()` calls, so an escape sequence or line split across two pipe reads still parses correctly.
- Allocated a 16-color ANSI palette as real `XftColor`s once at startup; bold is emulated as "use the bright variant of the current color" (no bold font variant is loaded).

**Known limitation (by design):** since the pipe-based process model was kept instead of switching to a real pty, tools that only auto-enable color when `isatty()` is true will still see a pipe and stay uncolored on their own  only forced color (`--color=always`, colored prompts, etc.) will render. Using `forkpty()`/`openpty()` would fix that too, but was intentionally ruled out: this project is being built from scratch, and those functions hand over exactly the terminal-behavior work (line discipline, tty detection, etc.) this project exists to implement manually.

### 3.2 Non-Latin Unicode glyphs didn't render

**Problem:** `main()` built a fallback list of fonts for different scripts (Devanagari, Bengali, Tamil, Arabic, CJK, emoji, ...) but the loop opened only the **first** one that succeeded and stopped  in practice always `DejaVu Sans Mono`  and used that single font for all text for the rest of the session. DejaVu Sans Mono doesn't cover most of those scripts, so any non-Latin character rendered as a tofu box or nothing, and the rest of the font list was dead code.

**Solution:**

- The font loop now opens *every* font in the list into `Font_List[]` instead of stopping at the first hit.
- Wrote a manual UTF-8 decoder (`Utf8_Decode`) and a per-codepoint font picker (`Pick_Font_For_Codepoint`, using `XftCharExists`)  no fontconfig-provided fallback chain, just an explicit scan through `Font_List`.
- `DrawUtf8Adv` groups consecutive codepoints that resolve to the same font into runs and draws/measures each run separately, so a single line (or even the built-in Devanagari search prompt) can mix scripts and each glyph is drawn with whichever font actually contains it.

**Verification:** Since there's no X server in the development sandbox to visually confirm the GUI, the pure parsing logic (SGR parsing, bold-brightening, escape/UTF-8 sequences split across reads, OSC swallowing, CRLF handling, color persistence across lines) was extracted into a standalone test harness and verified with assertions  all 8 cases passed. The GUI rendering itself (`DrawUtf8Adv`, font fallback) should still be checked visually with `make run` on a machine with a display.

## 5. Shell prompt (`user@host:path$`) was plain white, no way to color part of a line

**Problem:** Wanted the `user@host` (and path) portion of the prompt colored like a normal terminal, but two things stood in the way: `Get_Current_Path` ([headers/util.h](headers/util.h)) returns one plain `string`, and the live prompt line in `Draw_UI` was drawn with a single `DrawUtf8` call and one fixed `XftColor`  there was no per-substring coloring path for it, only for scrollback lines (which go through the streaming SGR parser in `Add_To_Buffer`).

**Solution:**

- Factored the reusable SGR primitives (`Parse_Int`, `Apply_SGR`, `Effective_Color`) out of [headers/ansi.h](headers/ansi.h) into a new [headers/ansi_core.h](headers/ansi_core.h), since `render.h` needed them too and `ansi.h` already depends on `render.h` (for `Draw_UI`)  pulling the primitives into a lower dependency-free header avoided a circular include.
- Added `Ansi_To_Segments(const string&)` to `ansi_core.h`: a one-shot version of the same SGR parsing used for streaming child output, but for a complete in-memory string (no partial-sequence/UTF-8-split handling needed, since the whole string is already available)  used for coloring small strings like the prompt rather than raw process output.
- `Get_Current_Path` now returns the prompt with embedded SGR codes: bold green for `user@host`, bold blue for the path (same colors as the default bash `PS1`).
- `Draw_UI`'s prompt-line rendering (and its cursor-position measurement) now run the prefix through `Ansi_To_Segments` and draw/measure each colored segment, instead of treating the whole prefix as one plain string.
- The four other places that echo `Get_Current_Path(...)` into scrollback (Ctrl-C, Ctrl-Z, Enter, Tab-completion listing) needed no changes  they already go through `Add_To_Buffer`, which has understood SGR codes since fix 3.1.

**Verification:** extended the standalone parser test harness with a case asserting a prompt string with embedded `\x1b[1;32m...\x1b[0m:\x1b[1;34m...\x1b[0m$ ` splits into the expected 4 colored segments  passed. Visual confirmation of the actual rendered colors still needs a real X session (see fix 3's verification note).

## 8. Interactive session support  real EOF, live input, and the shell prompt correctly hiding while a command runs

**Problem:** fix 7's `/dev/null` default solved the *hang*, but at the cost of interactivity: `cat`, `sort`, `wc` with no args, `read` loops  anything meant to take input from the terminal  got instant EOF and just exited immediately, same as `lz` did. There was no way to actually type something *for* a running command, and `Ctrl+D` never delivered a real EOF signal either, since it just wrote the literal byte `0x04` into the shell's persistent pipe  meaningful only under a pty's line discipline, which this project deliberately doesn't have (see [DESIGNDOC.md](DESIGNDOC.md) section 3.9, which now documents the resulting protocol as the current design).

**Solution:** replaced the blanket `/dev/null` default with a small protocol between `function.cpp` and `terminal.cpp`, entirely over the existing plain pipes (still no pty  see 9.2/6.2's notes on why):

- [function.cpp](function.cpp): when a foreground command (plain-command path only  pipes and `multiWatch` are out of scope, see the caveat in DESIGNDOC.md 6.2) gets no `<` redirection, it now gets a private pipe instead of `/dev/null`. `main()` was restructured from a single `while (getline(cin, line))` loop into a small state machine with two modes: *idle* (read and dispatch newline-terminated command lines, via a new `Read_Next_Line` helper that also replaced a second, easy-to-miss `getline(cin, ...)` inside `echo`'s multi-line quote handling) and *foreground-relay* (poll the shell's own stdin, forward whatever arrives straight into the running command's private pipe, and reap the child with `waitpid(..., WNOHANG)` instead of blocking). Byte `0x04` is now treated specially in relay mode: instead of being forwarded as data, it closes the private pipe's write end  a real EOF. The backend also emits `0x01` right before a foreground command starts and `0x02` right after it ends, so the frontend can track this state.
  - All input-stream reads were consolidated behind `Read_Next_Line`/raw `read()` calls specifically to avoid a subtle bug: mixing buffered `cin` reads with raw `read(STDIN_FILENO, ...)` calls on the same fd can silently drop bytes `cin` had already buffered ahead  which would have intermittently "lost" keystrokes meant for a running command.
- [headers/tab.h](headers/tab.h): added `Tab::foreground_busy`.
- [headers/ansi.h](headers/ansi.h): `Add_To_Buffer`'s byte parser now recognizes `0x01`/`0x02` alongside the existing ANSI/CSI/OSC handling, flipping `foreground_busy` without ever rendering them as text.
- [terminal.cpp](terminal.cpp): `Process()`'s key handler now has a `foreground_busy` branch that takes over *before* the normal shell-command-composition logic  printable characters, Backspace, and arrow keys still edit `T.line` locally exactly as before (reusing all the existing line-editing code, i.e. still line-buffered, not raw per-keystroke forwarding), but Enter sends the finished line straight to the running command (plainly echoed into scrollback, no shell-prompt prefix, not added to history, no `clear`/`exit` special-casing) instead of being parsed as a new shell command. `Ctrl+D` now only sends the EOF byte when the line is empty, matching normal shell convention, in both the busy and idle handlers.
- [headers/render.h](headers/render.h): `Draw_UI` shows no prompt prefix at all while `foreground_busy` is set, instead of the `user@host:path$` prompt (which would be misleading  you're not composing a shell command).

**Caveat (documented in DESIGNDOC.md 6.2):** this is line-buffered interactivity, not full raw-mode terminal support. Full-screen programs (`vim`, `less`, a nested `bash`/`python3` REPL with its own arrow-key history) still won't work correctly  that needs real tty semantics that only a pseudo-terminal provides, which this project is deliberately not adopting. A side effect worth knowing: a command that merely *reads* stdin without being meant to be interactive (`lz` from fix 7) no longer auto-resolves via `/dev/null`  it now waits for `Ctrl+D`/`Ctrl+C` like a real shell's foreground job would, which is intentional now that both are real, working escape hatches.

**Verification:** ran `function` standalone against a FIFO and fed it a sequence of commands via `printf`, confirming byte-for-byte: `cat` echoes back typed lines live; `Ctrl+D` (`\x04`) closes it cleanly and the shell immediately accepts and runs the next command (`echo AFTER_CAT` → `AFTER_CAT`); `lz` now waits instead of auto-exiting, and both `Ctrl+D` and `Ctrl+C` (`kill -INT`) correctly recover the shell from it; `< file` redirection, `cmd1 | cmd2` pipes, `cd`, `echo`, and `multiWatch` all still work unchanged. Both binaries build with zero warnings.

---

## 9. `exit` closed the whole program instead of just the current tab

**Problem:** typing `exit` set `Process()`'s `running` flag straight to `false`, tearing down the entire event loop  and with it every open tab  regardless of how many tabs were open. `Ctrl+W` already had the right behavior (close just the active tab, refusing to close the last one), `exit` just didn't follow it.

**Solution:** in [terminal.cpp](terminal.cpp)'s `exit` handling, mirror the `Ctrl+W` logic: if more than one tab is open, close only the active one (`Close_Tab(Active_Tab)`) and redraw; only quit the whole program when it's the last tab. Since `Close_Tab` destroys the `Tab` object `T` refers to, the handler `break`s immediately after closing rather than falling through to the trailing `T.history_index = ...` cleanup that runs after the if/else chain  the same care already taken by the existing `Ctrl+W` handler right above it.

**Verification:** `g++ -fsyntax-only` and a full `make` build clean; logic matches the already-working `Ctrl+W` path line-for-line for the "more than one tab" case.

### 12.1 `Tab` key on an empty command line quit the whole application

**Problem:** in `Process()`'s `KeyPress` handling ([terminal.cpp](terminal.cpp)), pressing Tab with an empty `T.line` hit `return;`  since `Process()` is called exactly once from `main()`, this returned out of the entire event loop, and `main()` fell straight through to teardown (`Cleanup_Shared_Memory()`/`Render_Cleanup()`/`XDestroyWindow`), closing every open tab instead of just doing nothing for that keypress.

**Solution:** changed the early-out to `break;`, matching how the `Ctrl+W`/`Ctrl+X` handlers in the same `switch (event.type) { case KeyPress: ... }` block already exit just the current case.

**Verification:** `make clean && make` builds clean; traced the enclosing `switch`/`case KeyPress` structure to confirm `break` exits only the key-handling case, not the outer `while` event loop that `Process()` runs.

### 12.2 A relayed keystroke to an already-exited foreground command could kill the whole shell backend

**Problem:** [function.cpp](function.cpp)'s foreground-relay loop (the interactive-session protocol from fix 8) writes each typed byte straight to the running command's private stdin pipe with a raw `write()`. If the child had already exited and closed its read end  plausible for any quick command (`ls`, etc.) before the `WNOHANG` reap runs  that `write()` raises `SIGPIPE`, and since nothing in the codebase ever set a disposition for it, the default action (terminate the process) killed the entire `function` backend, taking down that tab.

**Solution:** `main()` now calls `signal(SIGPIPE, SIG_IGN)` at startup, alongside the existing `SIGINT`/`SIGTSTP` handlers. The relay loop's existing `if (write(...) < 0) break;` already handles the resulting `-1`/`EPIPE` correctly  it just needed to not crash first.

**Verification:** `make clean && make` builds clean with zero warnings.

### 12.3 Foreground child leaked (never reaped, write fd never closed) if the frontend's own pipe closed mid-command

**Problem:** in the same relay loop, if `read()` on the shell's own stdin (from `terminal.cpp`) returned `<= 0`  the frontend's write end closing  the code set `g_foreground_pid = -1` directly. The very next check, `if (g_foreground_pid > 0) { ...waitpid... }`, then short-circuited on `-1`, so a foreground child that was still alive and running was never `waitpid`'d (leaked as a zombie once it did exit) and `g_interactive_write_fd` was never closed (leaked fd).

**Solution:** replaced the immediate `g_foreground_pid = -1` with a new `g_frontend_stdin_closed` flag. Once set, the loop stops polling/reading the now-dead stdin (sleeping 100ms between iterations instead) but keeps running the existing `WNOHANG` reap check every iteration, so the child still gets properly waited on and `g_interactive_write_fd` still gets closed whenever it actually exits. The flag is reset to `false` each time a new foreground command starts.

**Verification:** ran `function` standalone against a FIFO, wrote `cat\n`, then closed the FIFO's write end while `cat` was still blocked reading (simulating the frontend disappearing mid-command). Before the fix this would have abandoned tracking immediately; after the fix, `ps` confirms the shell process stays alive and `cat` remains its properly-tracked child (not orphaned) rather than being silently dropped.

### 12.4 Duplicated `/dev/null`-stdin-fallback block

**Problem:** the same 6-line "open `/dev/null`, `dup2` it onto stdin, close the fd" pattern (used so a child with no explicit `<` redirection gets immediate EOF instead of hanging on the shell's shared interactive pipe  see fix 7) was copy-pasted three times in [function.cpp](function.cpp): the `multiWatch` child, the first stage of a pipeline, and the interactive-pipe-setup failure fallback. Any future change to this logic had to be applied in three places by hand, which had already caused drift once (the `multiWatch` branch briefly missed `signal(SIGTSTP, SIG_DFL)`, fix 2.5).

**Solution:** factored it into a single `Redirect_Stdin_To_DevNull()` helper (next to the existing `Block_Watch_Signals`/`Restore_Watch_Signals` helpers) and replaced all three call sites with a call to it.

**Verification:** `make clean && make` builds clean with zero warnings; behavior at all three call sites is unchanged (same `open`/`dup2`/`close` sequence, just in one place).
