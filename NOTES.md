# 📝 Code Notes

This file holds the explanatory notes that used to live as inline comments in the source. The code itself has none now ~ this is where the "why" behind non-obvious decisions lives instead, organized by file and roughly by source order.

---

## function.cpp

**`g_pending_input` / `g_foreground_pid` / `g_interactive_write_fd`** ~ All input from the frontend (both new command lines and, once a foreground command is running, raw bytes meant for it) arrives on the same fd. These globals track that single stream so nothing ever mixes buffered `iostream` reads (`cin`) with raw `read(STDIN_FILENO, ...)` calls, which would silently drop bytes `cin` had already buffered ahead.

**`Redirect_Stdin_To_DevNull()`** ~ Gives the calling (post-fork, pre-exec) child real, immediate EOF on stdin instead of leaving it attached to whatever it inherited. Used everywhere a child gets no explicit `<` input redirection, since this shell has no way to forward keystrokes to a running background/multiWatch child anyway ~ leaving stdin attached to the shell's interactive pipe would let such a command silently steal bytes the user meant for the shell prompt, or block forever waiting for input that will never arrive.

**`Read_Next_Line()`** ~ Pulls one newline-terminated line off `g_pending_input`, reading more raw bytes from the shell's own stdin as needed. Returns `false` only on real EOF (the frontend closed the pipe). Used both for ordinary command dispatch and for `echo`'s multi-line quote continuation, so every read of this fd goes through the same buffer.

**`signal(SIGPIPE, SIG_IGN)` in `main()`** ~ Writes to a foreground child's private stdin pipe (in the relay loop) can hit a closed read end if the child already exited. Without ignoring `SIGPIPE`, the default disposition would kill the whole shell backend instead of just failing that one `write()` with `EPIPE`.

**The foreground-relay loop (`if (g_foreground_pid > 0)`)** ~ A foreground command reading stdin gets whatever the user types relayed straight to it (translating the EOF marker byte into a real close of its stdin pipe), while also being watched for exit ~ instead of blocking exclusively on either one.

**`g_frontend_stdin_closed`** ~ When the shell's own input pipe closes mid-command, there's nothing left to relay, but the foreground child may still be running. Rather than abandoning tracking immediately, this flag stops the poll/read attempts on the dead stdin while the existing `WNOHANG` reap check keeps running every iteration, so the child still gets properly waited on and `g_interactive_write_fd` still gets closed once it actually exits.

**`0x04` handling** ~ This is the EOF marker (Ctrl+D) sent by the frontend; it closes the interactive pipe's write end instead of being forwarded as a literal byte.

**`cout << '\x02'`** ~ Tells the frontend "back to a normal prompt" (foreground command finished).

**`cout << '\x01'`** ~ Tells the frontend "route input to this command" (a foreground command just started); `main()`'s relay loop takes over from there.

**multiWatch fork / pipeline first stage ~ `Redirect_Stdin_To_DevNull()` call sites** ~ Same reasoning as above: no code path forwards keystrokes to these children, so `/dev/null` costs nothing and gives any stdin-reading program instant, correct EOF instead of an indefinite hang.

**`WIFSTOPPED` branch in the multiWatch reap loop** ~ Deliberately keeps the fd/tempfile/`g_child_pid` entry intact: the child is still alive (just stopped via Ctrl+Z) and may still write, and it must remain signalable via `g_child_pid` since this shell has no `fg`/`bg` to resume it explicitly.

**No-`<`-redirection plain-command path** ~ Gives the command a private stdin pipe instead of either real `/dev/null` or the shell's own interactive input pipe directly. `main()`'s loop relays whatever the user types into this pipe live (and closes it for a real EOF on Ctrl+D) while the command runs, so ordinary line-reading interactive programs (`cat`, `sort`, `wc`, a `read` loop, ...) work correctly. Full-screen raw-mode programs (`vim`, `less`, a nested shell) still won't, since that needs actual tty semantics this pipe-based design deliberately doesn't provide (see DESIGNDOC.md 9.2/6.2).

**Interactive-pipe-setup failure fallback** ~ If `pipe()` itself failed above, fall back to `/dev/null` so the command still gets clean EOF instead of hanging.

---

## terminal.cpp

**`foreground_busy` branch in `Process()`'s key handler** ~ A foreground command is reading stdin (see `headers/ansi.h`'s `0x01`/`0x02` handling and `function.cpp`'s main loop). Keystrokes go to it instead of composing the next shell command: history, search, autocomplete, and the shell-prompt echo all make sense only when there's no running foreground command to send input to.

**`Add_To_Buffer(T, T.line + "\n")` on Enter while `foreground_busy`** ~ Plain echo, no shell prompt ~ this isn't a shell command, it's a line being sent to whatever's running in the foreground.

**`exit` handler, more-than-one-tab branch** ~ `Close_Tab` destroys `T`'s underlying `Tab` object, so nothing touches `T` again after this call. Mirrors the `Ctrl+W` handler right above it, which takes the same care.

---

## headers/tab.h

**`Tab::foreground_busy`** ~ Set/cleared by the `0x01`/`0x02` control bytes the shell backend sends around a foreground command that's reading stdin (see `headers/ansi.h`). While true, keystrokes route to that command instead of composing the next shell command line.

**`Tab::raw_pending`, `current_bold`, ... (ANSI parser state fields)** ~ Incoming child-process output is parsed byte-by-byte for ANSI SGR color codes; these fields hold the parser's state across separate `read()` calls, since an escape sequence or UTF-8 sequence can split across two reads.

---

## headers/ansi.h

**`Split_Incomplete_Utf8()`** ~ A `read()` from the child's pipe can land mid-UTF-8-sequence. If the tail of `text` looks like the start of a multi-byte codepoint that isn't finished, this moves those trailing bytes into `pending` so the next chunk can complete it.
  - The continuation-byte check (`(c & 0xC0) == 0x80`) just keeps scanning backwards over continuation bytes.
  - The `need = 1` fallback for an invalid lead byte just leaves that byte in place rather than trying to reassemble something meaningless.

**`Add_To_Buffer()`** ~ Parses raw child-process output for ANSI SGR color codes and appends the result to the tab's scrollback as colored segments. CSI sequences other than `"m"` (cursor movement, erase, ...) and OSC sequences are recognized only enough to be swallowed without corrupting the visible text ~ this terminal doesn't implement cursor addressing, so there's nothing meaningful to do with them.

**`0x01`/`0x02` handling inside `Add_To_Buffer`** ~ Control bytes the shell backend sends around a foreground command that's reading stdin (see `function.cpp`'s main loop): `0x01` = a foreground command just started, `0x02` = it finished. Never shown as text ~ just flips the routing flag `Process()` checks to decide where keystrokes go.

---

## headers/ansi_core.h

Pure ANSI/SGR color primitives with no dependency on `Tab` state or X11 ~ shared by `ansi.h` (streaming child-process output into `Add_To_Buffer`) and `render.h` (one-shot coloring of small in-memory strings like the prompt).

- **`Parse_Int`** ~ Turns `"0"`, `"1"`, `"31"` etc. into an int without throwing on garbage input.
- **`Apply_SGR`** ~ Applies one SGR (`ESC [ ... m`) parameter list to a running color/bold state. Only foreground color + bold are modeled; anything else (background, underline, blink, ...) is accepted and ignored (the `40-47`/`100-107` background codes fall through untouched for the same reason).
- **`Effective_Color`** ~ Bold historically means "use the bright variant of the current color."
- **`Ansi_To_Segments`** ~ One-shot conversion of a complete, already-in-memory string into colored segments. Unlike `Add_To_Buffer`, there's no streaming/partial-sequence handling here since the whole string is available up front ~ meant for small strings like the shell prompt, not raw child-process output.

---

## headers/util.h

**`Get_Current_Path()`'s prompt construction** ~ Bold green for `user@host`, bold blue for the path ~ same colors as the default bash prompt. These are plain SGR escape codes; `Add_To_Buffer` and the live prompt renderer both understand them (see `headers/ansi.h`, `headers/ansi_core.h`).

---

## headers/globals.h

- `gc` ~ the global X11 graphics context.
- `screen` ~ the UI screen number.
- `window` ~ the rectangle screen area (the terminal's X11 window handle).
- `display` ~ the `Display*` structure handling all X11 actions.
- `primaryAtom` ~ X11's `PRIMARY` selection: whatever text is currently highlighted/selected on screen (middle-click paste on Linux). (An Atom is a unique, server-registered integer ID representing a specific string.)
- `propertyAtom` ~ a scratch window property this terminal owns, used as the drop-off point where the X server delivers converted selection data (via `XConvertSelection`) before `ProcessPasteData` reads it back out.
- `clipboardAtom` ~ the explicit `CLIPBOARD` selection, filled by a deliberate copy (Ctrl+C in another app), read on Ctrl+V.
- `utf8StringAtom` ~ the data format requested when asking for a selection's contents: "give me the text as UTF-8," not the selection itself.
- **UI theme constants** (`COLOR_TABBAR_BG`, `COLOR_TAB_INACTIVE`, `COLOR_TAB_ACTIVE`, `COLOR_ACCENT`, `COLOR_ICON`, `COLOR_PLUS_BORDER`) ~ packed `0xAARRGGBB` pixel values, matching the TrueColor-visual assumption `Draw_UI` already makes (`0xFF2F2A29`, `BlackPixel`/`WhitePixel` passed straight to `XSetForeground`). Used only by `render.h`'s `Draw_UI`. Respectively: tab strip background, blends into the bar, lighter slate, active-tab top strip/divider/cursor, close(x)/plus(+) icon lines, plus-button box outline.

---

## headers/render.h

**`Xft_Color_Dim`** ~ Used for inactive-tab title text.

**`Font_List[16]`** ~ One font per script from the fallback list; a glyph is drawn with the first font in this list that actually contains it (see `Pick_Font_For_Codepoint`).

**`Point_Segment_Distance()`** ~ Shortest distance from `(px,py)` to the segment `(x1,y1)-(x2,y2)` ~ used to rasterize the icon's `">_"` glyph the same way the tab bar's close/plus icons are drawn: as a handful of line strokes, not an image asset.

**`Set_Window_Icon()`** ~ Builds a small ARGB icon (a `">_"` prompt glyph on a rounded dark square, using the same theme colors as the tab bar) and sets it as the window's `_NET_WM_ICON`, which is what window managers use for the taskbar, alt-tab switcher, and title bar ~ no image file or library involved, just pixels computed the same way the tab bar's hand-drawn icons are.
  - The `Glyph[]` strokes are, in order: the `>` upper diagonal, the `>` lower diagonal, and the `_` underscore.
  - Pixels outside the rounded corner are pushed as `0x00000000` ~ fully transparent.

**`Render_Init()`** ~ Opens the font fallback list, creates the `XftDraw` surface, and allocates the UI + ANSI colors. Must be called once display/window/screen are set up. Returns `false` (after logging why) if rendering can't be initialized.
  - The font-opening loop opens *every* font in the fallback list, not just the first that succeeds, so glyphs missing from the primary font can still be drawn by a later one in the list.
  - `dimColor = {0x6c6c, 0x7070, 0x8686, 0xffff}` matches `COLOR_ICON`'s muted slate.

**`Utf8_Decode()`** ~ Decodes one UTF-8 codepoint starting at `text[i]`; returns the number of bytes consumed (always ≥ 1, even for invalid/truncated input).

**`DrawUtf8Adv()`** ~ Draws `s`, choosing a font per run of consecutive codepoints so glyphs the primary font lacks (Devanagari, CJK, Arabic, emoji, ...) fall back to whichever font in `Font_List` actually has them. Pass `draw == nullptr` to only measure the advance (used for cursor placement) without actually drawing.

**`Truncate_To_Width()`** ~ Shortens `text` to fit within `max_width` pixels, appending an ellipsis if it had to cut anything. Measures with `DrawUtf8Adv` (`draw == nullptr`) instead of counting characters, since glyph widths vary across the font-fallback list.

**Active-tab top strip in `Draw_UI`** ~ A bright top strip on the active tab reads as "selected" at a glance, the way most editor/browser tab bars mark the current tab.

**Close button drawing** ~ Two crossed lines drawn directly, instead of a font glyph `"x"` whose weight/position would drift with whichever fallback font happened to be picked.

**New-tab (`"+"`) button drawing** ~ A bordered box with a crisp drawn cross, same treatment as the close button rather than a font glyph `"+"`.

**Prompt-prefix rendering** ~ `Prefix = ""` while `foreground_busy` because the user is typing input for a running command, not composing a shell command. `Get_Current_Path(...)` may embed SGR color codes (e.g. bold `user@host`), so the prompt prefix is drawn as colored segments the same way scrollback lines are ~ the typed-in line itself always stays the plain UI color.
