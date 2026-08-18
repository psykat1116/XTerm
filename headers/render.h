#pragma once

#include <cmath>
#include <string>
#include <vector>
#include <cstdint>
#include <iostream>
#include <algorithm>

#include <X11/Xft/Xft.h>

#include "tab.h"
#include "util.h"
#include "globals.h"
#include "ansi_core.h"

using namespace std;

XftColor Xft_Color;
XftColor Xft_Color_Dim;
XftFont *Xft_Font = nullptr;
XftDraw *Xft_Draw = nullptr;

Visual *g_visual = nullptr;
Colormap g_colormap = 0;

XftFont *Font_List[16];
int Font_Count = 0;

XftColor Ansi_Palette[(int)AnsiColor::Count];

bool Render_Init();
void Render_Cleanup();
void Draw_UI();
void Set_Window_Icon();
void DrawUtf8(XftDraw *, XftColor *, int, int, const string &);
void DrawUtf8Adv(XftDraw *, XftColor *, int &, int, const string &);

static double Point_Segment_Distance(double px, double py, double x1, double y1, double x2, double y2)
{
    double dx = x2 - x1, dy = y2 - y1;
    double len_sq = dx * dx + dy * dy;
    double t = len_sq > 0.0 ? ((px - x1) * dx + (py - y1) * dy) / len_sq : 0.0;
    t = max(0.0, min(1.0, t));
    double cx = x1 + t * dx, cy = y1 + t * dy;
    double ex = px - cx, ey = py - cy;
    return sqrt(ex * ex + ey * ey);
}

void Set_Window_Icon()
{
    const int Size = 32;
    const double Corner_Radius = 6.0;
    const double Stroke_Thickness = 2.2;

    const unsigned long Bg = COLOR_TABBAR_BG;
    const unsigned long Fg = COLOR_ACCENT;

    struct Stroke
    {
        double x1, y1, x2, y2;
    };
    const Stroke Glyph[] = {
        {9, 9, 17, 16},
        {17, 16, 9, 23},
        {19, 23, 27, 23},
    };

    vector<unsigned long> Icon_Data;
    Icon_Data.push_back(Size);
    Icon_Data.push_back(Size);

    for (int y = 0; y < Size; y++)
    {
        for (int x = 0; x < Size; x++)
        {
            double corner_cx = -1, corner_cy = -1;
            if (x < Corner_Radius && y < Corner_Radius)
            {
                corner_cx = Corner_Radius;
                corner_cy = Corner_Radius;
            }
            else if (x >= Size - Corner_Radius && y < Corner_Radius)
            {
                corner_cx = Size - Corner_Radius - 1;
                corner_cy = Corner_Radius;
            }
            else if (x < Corner_Radius && y >= Size - Corner_Radius)
            {
                corner_cx = Corner_Radius;
                corner_cy = Size - Corner_Radius - 1;
            }
            else if (x >= Size - Corner_Radius && y >= Size - Corner_Radius)
            {
                corner_cx = Size - Corner_Radius - 1;
                corner_cy = Size - Corner_Radius - 1;
            }

            bool Outside_Rounded_Corner = corner_cx >= 0 &&
                                           sqrt((x - corner_cx) * (x - corner_cx) + (y - corner_cy) * (y - corner_cy)) > Corner_Radius;

            if (Outside_Rounded_Corner)
            {
                Icon_Data.push_back(0x00000000);
                continue;
            }

            bool On_Glyph = false;
            for (auto &s : Glyph)
            {
                if (Point_Segment_Distance(x + 0.5, y + 0.5, s.x1, s.y1, s.x2, s.y2) <= Stroke_Thickness)
                {
                    On_Glyph = true;
                    break;
                }
            }
            Icon_Data.push_back(On_Glyph ? Fg : Bg);
        }
    }

    Atom Net_Wm_Icon = XInternAtom(display, "_NET_WM_ICON", False);
    Atom Cardinal = XInternAtom(display, "CARDINAL", False);
    XChangeProperty(display, window, Net_Wm_Icon, Cardinal, 32, PropModeReplace,
                     (unsigned char *)Icon_Data.data(), (int)Icon_Data.size());
}

bool Render_Init()
{
    const char *font_name_list[] = {
        "DejaVu Sans Mono:size=12",
        "Noto Sans Devanagari:size=12",
        "Noto Sans Bengali:size=12",
        "Noto Sans Tamil:size=12",
        "Noto Sans Arabic:size=12",
        "Noto Sans CJK SC:size=12",
        "Noto Sans CJK TC:size=12",
        "Noto Sans CJK JP:size=12",
        "Noto Sans CJK KR:size=12",
        "Noto Color Emoji:size=12",
        "Noto Sans Mono:size=12",
        "Monospace:size=12"};

    Font_Count = 0;
    for (auto &fname : font_name_list)
    {
        XftFont *f = XftFontOpenName(display, screen, fname);
        if (f)
            Font_List[Font_Count++] = f;
    }
    Xft_Font = Font_Count > 0 ? Font_List[0] : nullptr;
    if (!Xft_Font)
    {
        cerr << "Fatal Error: No Xft font available" << endl;
        return false;
    }

    g_visual = DefaultVisual(display, screen);
    g_colormap = DefaultColormap(display, screen);

    Xft_Draw = XftDrawCreate(display, window, g_visual, g_colormap);
    if (!Xft_Draw)
    {
        cerr << "Fatal Error: XftDrawCreate failed" << endl;
        XftFontClose(display, Xft_Font);
        return false;
    }

    XRenderColor renderColor = {0xffff, 0xffff, 0xffff, 0xffff};
    if (!XftColorAllocValue(display, g_visual, g_colormap, &renderColor, &Xft_Color))
        cerr << "Warning: XftColorAllocValue failed" << endl;

    XRenderColor dimColor = {0x6c6c, 0x7070, 0x8686, 0xffff};
    if (!XftColorAllocValue(display, g_visual, g_colormap, &dimColor, &Xft_Color_Dim))
        cerr << "Warning: Xft_Color_Dim allocation failed" << endl;

    struct
    {
        AnsiColor color;
        unsigned char r, g, b;
    } Palette_Defs[] = {
        {AnsiColor::Default, 0xe5, 0xe5, 0xe5},
        {AnsiColor::Black, 0x00, 0x00, 0x00},
        {AnsiColor::Red, 0xcd, 0x00, 0x00},
        {AnsiColor::Green, 0x00, 0xcd, 0x00},
        {AnsiColor::Yellow, 0xcd, 0xcd, 0x00},
        {AnsiColor::Blue, 0x00, 0x00, 0xee},
        {AnsiColor::Magenta, 0xcd, 0x00, 0xcd},
        {AnsiColor::Cyan, 0x00, 0xcd, 0xcd},
        {AnsiColor::White, 0xe5, 0xe5, 0xe5},
        {AnsiColor::BrightBlack, 0x7f, 0x7f, 0x7f},
        {AnsiColor::BrightRed, 0xff, 0x00, 0x00},
        {AnsiColor::BrightGreen, 0x00, 0xff, 0x00},
        {AnsiColor::BrightYellow, 0xff, 0xff, 0x00},
        {AnsiColor::BrightBlue, 0x5c, 0x5c, 0xff},
        {AnsiColor::BrightMagenta, 0xff, 0x00, 0xff},
        {AnsiColor::BrightCyan, 0x00, 0xff, 0xff},
        {AnsiColor::BrightWhite, 0xff, 0xff, 0xff},
    };
    for (auto &d : Palette_Defs)
    {
        XRenderColor rc = {(unsigned short)(d.r * 257), (unsigned short)(d.g * 257), (unsigned short)(d.b * 257), 0xffff};
        if (!XftColorAllocValue(display, g_visual, g_colormap, &rc, &Ansi_Palette[(int)d.color]))
            cerr << "Warning: Ansi_Palette allocation failed" << endl;
    }

    return true;
}

void Render_Cleanup()
{
    if (Xft_Draw)
    {
        XftDrawDestroy(Xft_Draw);
        Xft_Draw = nullptr;
    }
    for (int i = 0; i < Font_Count; i++)
    {
        if (Font_List[i])
            XftFontClose(display, Font_List[i]);
        Font_List[i] = nullptr;
    }
    Xft_Font = nullptr;

    if (g_visual)
    {
        XftColorFree(display, g_visual, g_colormap, &Xft_Color);
        XftColorFree(display, g_visual, g_colormap, &Xft_Color_Dim);
        for (int i = 0; i < (int)AnsiColor::Count; i++)
            XftColorFree(display, g_visual, g_colormap, &Ansi_Palette[i]);
    }
}

static int Utf8_Decode(const string &text, size_t i, uint32_t &cp)
{
    unsigned char c = text[i];
    int len;
    uint32_t val;

    if ((c & 0x80) == 0x00)
    {
        cp = c;
        return 1;
    }
    else if ((c & 0xE0) == 0xC0)
    {
        len = 2;
        val = c & 0x1F;
    }
    else if ((c & 0xF0) == 0xE0)
    {
        len = 3;
        val = c & 0x0F;
    }
    else if ((c & 0xF8) == 0xF0)
    {
        len = 4;
        val = c & 0x07;
    }
    else
    {
        cp = c;
        return 1;
    }

    if (i + len > text.size())
    {
        cp = c;
        return 1;
    }

    for (int k = 1; k < len; k++)
    {
        unsigned char cc = text[i + k];
        if ((cc & 0xC0) != 0x80)
        {
            cp = c;
            return 1;
        }
        val = (val << 6) | (cc & 0x3F);
    }

    cp = val;
    return len;
}

static XftFont *Pick_Font_For_Codepoint(uint32_t cp)
{
    for (int i = 0; i < Font_Count; i++)
        if (Font_List[i] && XftCharExists(display, Font_List[i], (FcChar32)cp))
            return Font_List[i];
    return Font_Count > 0 ? Font_List[0] : nullptr;
}

void DrawUtf8Adv(XftDraw *draw, XftColor *color, int &x, int y, const string &s)
{
    if (s.empty() || Font_Count == 0)
        return;

    size_t i = 0;
    while (i < s.size())
    {
        uint32_t cp;
        int len = Utf8_Decode(s, i, cp);
        XftFont *run_font = Pick_Font_For_Codepoint(cp);

        size_t run_start = i;
        i += len;
        while (i < s.size())
        {
            uint32_t cp2;
            int len2 = Utf8_Decode(s, i, cp2);
            if (Pick_Font_For_Codepoint(cp2) != run_font)
                break;
            i += len2;
        }

        string run = s.substr(run_start, i - run_start);
        if (!run_font)
            continue;

        if (draw)
            XftDrawStringUtf8(draw, color, run_font, x, y, (const FcChar8 *)run.c_str(), (int)run.size());

        XGlyphInfo ext;
        XftTextExtentsUtf8(display, run_font, (FcChar8 *)run.c_str(), (int)run.size(), &ext);
        x += ext.xOff;
    }
}

void DrawUtf8(XftDraw *draw, XftColor *color, int x, int y, const string &s) {
    DrawUtf8Adv(draw, color, x, y, s);
}

static string Truncate_To_Width(const string &text, int max_width)
{
    if (max_width <= 0)
        return "";

    int full_width = 0;
    DrawUtf8Adv(nullptr, nullptr, full_width, 0, text);
    if (full_width <= max_width)
        return text;

    const string ellipsis = "…";
    int ellipsis_width = 0;
    DrawUtf8Adv(nullptr, nullptr, ellipsis_width, 0, ellipsis);

    string result;
    int width = 0;
    size_t i = 0;
    while (i < text.size())
    {
        uint32_t cp;
        int len = Utf8_Decode(text, i, cp);
        string ch = text.substr(i, len);

        int ch_width = 0;
        DrawUtf8Adv(nullptr, nullptr, ch_width, 0, ch);

        if (width + ch_width + ellipsis_width > max_width)
            break;

        result += ch;
        width += ch_width;
        i += len;
    }

    return result + ellipsis;
}

void Draw_UI()
{
    XClearWindow(display, window);
    XSetForeground(display, gc, COLOR_TABBAR_BG);
    XFillRectangle(display, window, gc, 0, 0, Win_Width, Tab_Height);

    int Tab_X = 0;
    for (size_t i = 0; i < Tabs.size(); i++)
    {
        bool active = (int)i == Active_Tab;
        int Tab_W = TAB_WIDTH;

        XSetForeground(display, gc, active ? COLOR_TAB_ACTIVE : COLOR_TAB_INACTIVE);
        XFillRectangle(display, window, gc, Tab_X + 1, 1, Tab_W - 1, Tab_Height - 2);

        if (active)
        {
            XSetForeground(display, gc, COLOR_ACCENT);
            XFillRectangle(display, window, gc, Tab_X + 1, 1, Tab_W - 1, 3);
        }

        int Close_X = Tab_X + Tab_W - CLOSE_BOX_W - 6;
        int Close_Y = (Tab_Height - CLOSE_BOX_H) / 2;

        int Title_Max_W = Close_X - (Tab_X + TAB_PADDING_L) - 6;
        string Tab_Title = Truncate_To_Width(Tabs[i]->title, Title_Max_W);
        DrawUtf8(Xft_Draw, active ? &Xft_Color : &Xft_Color_Dim, Tab_X + TAB_PADDING_L, Tab_Height - 8, Tab_Title);

        XSetForeground(display, gc, COLOR_ICON);
        XSetLineAttributes(display, gc, 2, LineSolid, CapRound, JoinRound);
        int Close_Pad = 3;
        XDrawLine(display, window, gc, Close_X + Close_Pad, Close_Y + Close_Pad, Close_X + CLOSE_BOX_W - Close_Pad, Close_Y + CLOSE_BOX_H - Close_Pad);
        XDrawLine(display, window, gc, Close_X + CLOSE_BOX_W - Close_Pad, Close_Y + Close_Pad, Close_X + Close_Pad, Close_Y + CLOSE_BOX_H - Close_Pad);

        Tab_X += Tab_W;
    }

    int Plus_X = Tab_X + 8;
    int Plus_Y = (Tab_Height - PLUS_BOX_H) / 2;
    XSetForeground(display, gc, COLOR_PLUS_BORDER);
    XSetLineAttributes(display, gc, 1, LineSolid, CapButt, JoinMiter);
    XDrawRectangle(display, window, gc, Plus_X, Plus_Y, PLUS_BOX_W, PLUS_BOX_H);

    XSetForeground(display, gc, COLOR_ICON);
    XSetLineAttributes(display, gc, 2, LineSolid, CapRound, JoinRound);
    int Plus_Cx = Plus_X + PLUS_BOX_W / 2, Plus_Cy = Plus_Y + PLUS_BOX_H / 2, Plus_Arm = PLUS_BOX_W / 2 - 4;
    XDrawLine(display, window, gc, Plus_Cx - Plus_Arm, Plus_Cy, Plus_Cx + Plus_Arm, Plus_Cy);
    XDrawLine(display, window, gc, Plus_Cx, Plus_Cy - Plus_Arm, Plus_Cx, Plus_Cy + Plus_Arm);

    XSetLineAttributes(display, gc, 1, LineSolid, CapButt, JoinMiter);
    XSetForeground(display, gc, COLOR_ACCENT);
    XDrawLine(display, window, gc, 0, Tab_Height, Win_Width, Tab_Height);

    if (Tabs.empty())
    {
        DrawUtf8(Xft_Draw, &Xft_Color_Dim, 8, Tab_Height + 20, "No tabs, press Ctrl+T to open one");
        XFlush(display);
        return;
    }

    Tab &T = *Tabs[Active_Tab];
    int font_ascent = Xft_Font ? Xft_Font->ascent : 12;
    int font_descent = Xft_Font ? Xft_Font->descent : 6;
    int line_height = font_ascent + font_descent;

    int Y = Tab_Height + line_height;
    bool Has_Partial_Line = !T.current_line.empty();
    int Total_Lines = (int)T.buffer.size() + (Has_Partial_Line ? 1 : 0);
    int Max_Lines_Visible = (Win_Height - Tab_Height - font_ascent - font_descent) / line_height;

    if (T.scroll_index > Total_Lines - Max_Lines_Visible)
        T.scroll_index = max(0, Total_Lines - Max_Lines_Visible);

    for (int i = T.scroll_index; i < Total_Lines && Y < Win_Height; i++)
    {
        const vector<Segment> &Line = (i < (int)T.buffer.size()) ? T.buffer[i] : T.current_line;

        int x = 5;
        int chars_drawn = 0;
        for (auto &seg : Line)
        {
            string text = seg.text;
            if (chars_drawn + (int)text.size() > 1000)
                text = text.substr(0, max(0, 1000 - chars_drawn));
            if (text.empty())
                break;
            chars_drawn += (int)text.size();

            DrawUtf8Adv(Xft_Draw, &Ansi_Palette[(int)seg.color], x, Y, text);
        }
        Y += line_height;
    }

    string Prefix;
    if (T.foreground_busy)
        Prefix = "";
    else if (T.is_search)
        Prefix = "एक दिन पहले मेरा दोसत अलासका गया था: ";
    else if (T.is_autocomplete)
        Prefix = "> ";
    else
        Prefix = Get_Current_Path(T.child_pid);

    vector<Segment> Prefix_Segments = AnsiToSegments(Prefix);

    int Prompt_X = 5;
    for (auto &seg : Prefix_Segments)
        DrawUtf8Adv(Xft_Draw, &Ansi_Palette[(int)seg.color], Prompt_X, Y, seg.text);
    DrawUtf8Adv(Xft_Draw, &Xft_Color, Prompt_X, Y, T.line);

    string Line_Upto_Cursor = T.line.substr(0, min((size_t)T.writing_index, T.line.size()));
    int Cursor_X, Cursor_Y = Y - font_ascent + 2;
    if (Xft_Font)
    {
        int measured_x = 5;
        for (auto &seg : Prefix_Segments)
            DrawUtf8Adv(nullptr, &Xft_Color, measured_x, Y, seg.text);
        DrawUtf8Adv(nullptr, &Xft_Color, measured_x, Y, Line_Upto_Cursor);
        Cursor_X = measured_x;
    }
    else
    {
        size_t Prefix_Visible_Len = 0;
        for (auto &seg : Prefix_Segments)
            Prefix_Visible_Len += seg.text.size();
        Cursor_X = 5 + (int)((Prefix_Visible_Len + Line_Upto_Cursor.size()) * 8);
    }

    if (Cursor_Visible && !T.is_autocomplete)
    {
        int cursor_h = line_height - 4;
        if (cursor_h < 1)
            cursor_h = line_height;
        XSetForeground(display, gc, COLOR_ACCENT);
        XFillRectangle(display, window, gc, Cursor_X, Cursor_Y, 2, cursor_h);
    }

    XFlush(display);
}
