#pragma once

#include <string>
#include <algorithm>

#include "tab.h"
#include "render.h"
#include "globals.h"
#include "ansi_core.h"

using namespace std;

// Append text to the tab
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

// If text buffer overflow then trim old text
static void FlushLine(Tab &t)
{
    t.buffer.push_back(t.current_line);
    t.current_line.clear();
    if (t.buffer.size() > 4000)
        t.buffer.erase(t.buffer.begin(), t.buffer.begin() + 1000);
}

// If UTF-8 is incompleted then trim the incompleted part.
// Incompleted part will be conve in this next part.
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

// Add input to tab buffer for printing out data
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
        else
        {
            text_run += (char)c;
            i++;
        }
    }

    SplitIncompleteUtf8(text_run, t.raw_pending);
    flush_text_run();

    int font_ascent = Xft_Font ? Xft_Font->ascent : 12;
    int font_descent = Xft_Font ? Xft_Font->descent : 6;
    int Max_Lines_Visible = (Win_Height - Tab_Height - font_ascent - font_descent) / (font_ascent + font_descent);
    t.scroll_index = max(0, (int)t.buffer.size() - Max_Lines_Visible);

    Draw_UI();
}
