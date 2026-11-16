#pragma once

#include <string>
#include <algorithm>

#include "tab.h"
#include "render.h"
#include "globals.h"
#include "ansi_core.h"

using namespace std;

/**
 * @brief Append plain text to the tab's in-progress line using the tab's current style.
 *
 * The text is merged into the last segment when the effective color matches,
 * otherwise a new segment is started.
 *
 * @param t    Tab whose current line receives the text.
 * @param text Plain text without escape sequences or newlines.
 */
static void AppendText(Tab &t, const string &text)
{
    if (text.empty())
        return;

    AnsiColor col = EffectiveColor(t.current_color, t.current_bold);
    if (!t.current_line.empty() && t.current_line.back().color == col)
        t.current_line.back().text += text;
    else
        t.current_line.push_back({text, col});
}

/**
 * @brief Commit the tab's in-progress line to its scrollback buffer.
 *
 * When the buffer grows past 4000 lines, the oldest 1000 lines are discarded.
 *
 * @param t Tab whose current line is committed.
 */
static void FlushLine(Tab &t)
{
    t.buffer.push_back(t.current_line);
    t.current_line.clear();
    if (t.buffer.size() > 4000)
    {
        t.buffer.erase(t.buffer.begin(), t.buffer.begin() + 1000);

        // Keep a selection in this tab pointing at the same text.
        if (Selection.tab == &t)
        {
            Selection.anchor_row -= 1000;
            Selection.head_row -= 1000;
            if (Selection.anchor_row < 0 || Selection.head_row < 0)
                Clear_Selection();
        }
    }
}

/**
 * @brief Move a trailing, incomplete UTF-8 sequence from @p text into @p pending.
 *
 * Output from the child can be split in the middle of a multi-byte character.
 * The incomplete tail is held back so it can be prepended to the next read.
 *
 * @param[in,out] text    Text to check; truncated if it ends mid-character.
 * @param[out]    pending Receives the incomplete trailing bytes, if any.
 */
static void SplitIncompleteUtf8(string &text, string &pending)
{
    if (text.empty())
        return;

    size_t back_scan = min<size_t>(4, text.size());
    for (size_t k = 1; k <= back_scan; k++)
    {
        unsigned char c = text[text.size() - k];
        if ((c & 0xC0) == 0x80)
            continue;

        int need;
        if ((c & 0x80) == 0x00)
            need = 1;
        else if ((c & 0xE0) == 0xC0)
            need = 2;
        else if ((c & 0xF0) == 0xE0)
            need = 3;
        else if ((c & 0xF8) == 0xF0)
            need = 4;
        else
            need = 1;

        if (need > (int)k)
        {
            pending = text.substr(text.size() - k);
            text.resize(text.size() - k);
        }

        break;
    }
}

/**
 * @brief Feed raw output from the shell backend into a tab's display buffer.
 *
 * Parses the stream incrementally: SGR sequences update the tab's style, other
 * CSI and OSC sequences are dropped, '\\n' commits a line and '\\r' is ignored.
 * The control bytes 0x01 and 0x02 mark the start and end of a foreground
 * process and set Tab::foreground_busy; #CONTINUATION_START / #CONTINUATION_END
 * set Tab::continuation. Incomplete escape sequences and UTF-8
 * characters are kept in Tab::raw_pending for the next call. Afterwards the tab
 * is scrolled to the bottom and the UI is redrawn.
 *
 * @param t    Tab receiving the output.
 * @param data Raw bytes read from the backend.
 */
void AddToBuffer(Tab &t, const string &data)
{
    string input = t.raw_pending + data;
    t.raw_pending.clear();

    string text_run;
    size_t i = 0, n = input.size();
    auto flush_text_run = [&]()
    {
        if (!text_run.empty())
        {
            AppendText(t, text_run);
            text_run.clear();
        }
    };

    while (i < n)
    {
        unsigned char c = input[i];
        if (c == '\x1b')
        {
            if (i + 1 >= n)
            {
                flush_text_run();
                t.raw_pending = input.substr(i);
                break;
            }

            char next = input[i + 1];
            if (next == '[')
            {
                size_t j = i + 2;
                while (j < n && !(input[j] >= 0x40 && input[j] <= 0x7e))
                    j++;
                if (j >= n)
                {
                    flush_text_run();
                    t.raw_pending = input.substr(i);
                    break;
                }

                string params = input.substr(i + 2, j - (i + 2));
                char final_byte = input[j];
                flush_text_run();
                if (final_byte == 'm')
                    ApplySGR(t.current_color, t.current_bold, params);
                i = j + 1;
                continue;
            }
            else if (next == ']')
            {
                size_t j = i + 2;
                while (j < n && input[j] != '\x07' && !(input[j] == '\x1b' && j + 1 < n && input[j + 1] == '\\'))
                    j++;
                if (j >= n)
                {
                    flush_text_run();
                    t.raw_pending = input.substr(i);
                    break;
                }
                flush_text_run();
                i = (input[j] == '\x07') ? j + 1 : j + 2;
                continue;
            }
            else
            {
                flush_text_run();
                i += 2;
                continue;
            }
        }
        else if (c == '\n')
        {
            flush_text_run();
            FlushLine(t);
            i++;
        }
        else if (c == '\r')
        {
            flush_text_run();
            i++;
        }
        else if (c == '\x01' || c == '\x02')
        {
            flush_text_run();
            t.foreground_busy = (c == '\x01');
            i++;
        }
        else if (c == CONTINUATION_START || c == CONTINUATION_END)
        {
            flush_text_run();
            t.continuation = (c == CONTINUATION_START);
            i++;
        }
        else
        {
            text_run += (char)c;
            i++;
        }
    }

    SplitIncompleteUtf8(text_run, t.raw_pending);
    flush_text_run();

    t.scroll_index = max(0, (int)t.buffer.size() - Visible_Line_Count());

    Draw_UI();
}
