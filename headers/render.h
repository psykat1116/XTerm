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
#include "theme.h"
#include "globals.h"
#include "ansi_core.h"

using namespace std;

XftColor Xft_Color;     ///< Theme foreground (default text).
XftColor Xft_Color_Dim; ///< Theme muted text (inactive tabs, hints).
XftColor Xft_Color_Bg;  ///< Theme background, used for the character under the block cursor.
XftFont *Xft_Font = nullptr;
XftDraw *Xft_Draw = nullptr;

Colormap g_colormap = 0;
Visual *g_visual = nullptr;
bool Theme_Colors_Allocated = false;

int Font_Count = 0;
XftFont *Font_List[16];
XftColor Ansi_Palette[(int)AnsiColor::Count];

void Draw_UI();
bool Render_Init();
void Render_Cleanup();
void Set_Window_Icon();
void Free_Theme_Colors();
void Alloc_Theme_Colors();
void DrawUtf8(XftDraw *, XftColor *, int, int, const string &);
void DrawUtf8Adv(XftDraw *, XftColor *, int &, int, const string &);

/**
 * @brief Compute the shortest distance from a point to a line segment.
 *
 * @param px X coordinate of the point.
 * @param py Y coordinate of the point.
 * @param x1 X coordinate of the segment start.
 * @param y1 Y coordinate of the segment start.
 * @param x2 X coordinate of the segment end.
 * @param y2 Y coordinate of the segment end.
 * @return Euclidean distance from (px, py) to the nearest point on the segment.
 */
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

/**
 * @brief Draw a 32x32 ">_" prompt icon and set it as the window's _NET_WM_ICON.
 *
 * The icon is a rounded square in the tab bar color with the glyph drawn in
 * the accent color. Pixels outside the rounded corners are transparent.
 */
void Set_Window_Icon()
{
    const int Size = 32;
    const double Corner_Radius = 6.0;
    const double Stroke_Thickness = 2.2;

    const unsigned long Fg = 0xFF000000 | Current_Theme().accent;
    const unsigned long Bg = 0xFF000000 | Current_Theme().tabbar_bg;

    /** @brief A line segment of the icon glyph, in pixel coordinates. */
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

            bool Outside_Rounded_Corner = corner_cx >= 0 && sqrt((x - corner_cx) * (x - corner_cx) + (y - corner_cy) * (y - corner_cy)) > Corner_Radius;
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
    XChangeProperty(display, window, Net_Wm_Icon, Cardinal, 32, PropModeReplace, (unsigned char *)Icon_Data.data(), (int)Icon_Data.size());
}

/**
 * @brief Load fonts, create the Xft draw context and allocate colors.
 *
 * Opens a list of fallback fonts (DejaVu Sans Mono first, then Noto fonts for
 * other scripts and emoji), creates #Xft_Draw for the main window, and
 * allocates the text colors and the #Ansi_Palette. Color allocation failures
 * are reported as warnings only.
 *
 * @return false if no font could be opened or the draw context could not be created.
 */
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

    Load_Saved_Theme();
    Alloc_Theme_Colors();
    XSetWindowBackground(display, window, Current_Theme().bg);
    Set_Window_Icon();

    return true;
}

/**
 * @brief Allocate an Xft color from a 0xRRGGBB value.
 */
static void Alloc_Rgb(unsigned long rgb, XftColor *out)
{
    XRenderColor rc = {
        (unsigned short)(((rgb >> 16) & 0xff) * 257),
        (unsigned short)(((rgb >> 8) & 0xff) * 257),
        (unsigned short)((rgb & 0xff) * 257),
        0xffff};
    if (!XftColorAllocValue(display, g_visual, g_colormap, &rc, out))
        cerr << "Warning: XftColorAllocValue failed" << endl;
}

/**
 * @brief Allocate the text colors and #Ansi_Palette for the current theme.
 */
void Alloc_Theme_Colors()
{
    const Theme &th = Current_Theme();
    Alloc_Rgb(th.fg, &Xft_Color);
    Alloc_Rgb(th.dim, &Xft_Color_Dim);
    Alloc_Rgb(th.bg, &Xft_Color_Bg);

    Alloc_Rgb(th.fg, &Ansi_Palette[(int)AnsiColor::Default]);
    for (int i = 0; i < 16; i++)
        Alloc_Rgb(th.ansi[i], &Ansi_Palette[(int)AnsiColor::Black + i]);

    Alloc_Rgb(th.prompt_host, &Ansi_Palette[(int)AnsiColor::PromptHost]);
    Alloc_Rgb(th.dim, &Ansi_Palette[(int)AnsiColor::PromptParent]);
    Alloc_Rgb(th.prompt_dir, &Ansi_Palette[(int)AnsiColor::PromptDir]);
    Alloc_Rgb(th.prompt_arrow, &Ansi_Palette[(int)AnsiColor::PromptArrow]);

    Theme_Colors_Allocated = true;
}

/**
 * @brief Free the colors allocated by Alloc_Theme_Colors().
 */
void Free_Theme_Colors()
{
    if (!Theme_Colors_Allocated || !g_visual)
        return;

    XftColorFree(display, g_visual, g_colormap, &Xft_Color);
    XftColorFree(display, g_visual, g_colormap, &Xft_Color_Dim);
    XftColorFree(display, g_visual, g_colormap, &Xft_Color_Bg);
    for (int i = 0; i < (int)AnsiColor::Count; i++)
        XftColorFree(display, g_visual, g_colormap, &Ansi_Palette[i]);

    Theme_Colors_Allocated = false;
}

/**
 * @brief Switch to another theme, save the choice and redraw.
 *
 * Existing scrollback is recolored too, since lines store ANSI color indices
 * rather than RGB values.
 *
 * @param index Index into #THEMES.
 */
void Apply_Theme(int index)
{
    if (index < 0 || index >= THEME_COUNT)
        return;

    Free_Theme_Colors();
    Current_Theme_Index = index;
    Alloc_Theme_Colors();
    
    XSetWindowBackground(display, window, Current_Theme().bg);
    Set_Window_Icon();
    Save_Theme();
    Draw_UI();
}

/**
 * @brief Run the built-in "/theme" command.
 *
 * - "/theme" lists the themes and marks the current one.
 * - "/theme next" cycles to the next theme.
 * - "/theme prev" cycles to the previous theme.
 * - "/theme <name>" switches to that theme (a unique prefix is enough).
 *
 * @param args Everything after "/theme", already trimmed.
 * @return Text to print in the terminal, with ANSI colors and a trailing newline.
 */
string Theme_Command(const string &args)
{
    if (args.empty())
    {
        string out = "Themes:\n";
        for (int i = 0; i < THEME_COUNT; i++)
        {
            string label = THEMES[i].name;
            string tag = Is_Light_Theme(THEMES[i]) ? " \x1b[201m(light)\x1b[0m" : "";
            if (i == Current_Theme_Index)
                out += "  \x1b[203m● " + label + "\x1b[0m" + tag + "\n";
            else
                out += "    " + label + tag + "\n";
        }
        out += "\x1b[201mUsage: /theme <name> | /theme next\x1b[0m\n | /theme prev\x1b[0m\n";
        return out;
    }

    int index = -1;
    if (Normalize_Theme_Name(args) == "next")
        index = (Current_Theme_Index + 1) % THEME_COUNT;
    else if (Normalize_Theme_Name(args) == "prev")
        index = Current_Theme_Index == 0 ? THEME_COUNT - 1 : (Current_Theme_Index - 1) % THEME_COUNT;
    else
    {
        vector<int> matches = Theme_Matches(args);
        if (matches.size() > 1)
        {
            string out = "\x1b[33m'" + args + "' matches several themes:\x1b[0m";
            for (int m : matches)
                out += " " + string(THEMES[m].name);
            return out + "\n";
        }
        if (matches.size() == 1)
            index = matches[0];
    }

    if (index < 0)
        return "\x1b[31mUnknown theme '" + args + "'.\x1b[0m Type /theme to see the list.\n";

    Apply_Theme(index);
    return "Theme set to \x1b[203m" + string(THEMES[index].name) + "\x1b[0m\n";
}

/** @brief Height of one text line in pixels, including #LINE_GAP. */
int Line_Height()
{
    int ascent = Xft_Font ? Xft_Font->ascent : 12;
    int descent = Xft_Font ? Xft_Font->descent : 6;
    return ascent + descent + LINE_GAP;
}

/**
 * @brief Number of scrollback lines that fit above the prompt line.
 */
int Visible_Line_Count()
{
    int area = Win_Height - Tab_Height - CONTENT_PAD_TOP - CONTENT_PAD_BOTTOM;
    return max(1, area / Line_Height() - 1);
}

/**
 * @brief Scroll a tab so its newest output and the prompt line are in view.
 */
void Scroll_To_Bottom(Tab &T)
{
    int Total_Lines = (int)T.buffer.size() + (T.current_line.empty() ? 0 : 1);
    T.scroll_index = max(0, Total_Lines - Visible_Line_Count());
}

/**
 * @brief Release the resources allocated by Render_Init().
 *
 * Destroys the draw context, closes all fonts and frees all allocated colors.
 */
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
    Free_Theme_Colors();
}

/**
 * @brief Decode one UTF-8 code point starting at byte offset @p i.
 *
 * Invalid or truncated sequences are treated as a single byte whose value is
 * returned as the code point.
 *
 * @param text Input UTF-8 string.
 * @param i    Byte offset of the first byte to decode; must be less than text.size().
 * @param[out] cp Decoded code point.
 * @return Number of bytes consumed (1-4).
 */
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

/**
 * @brief Find the first loaded font that has a glyph for a code point.
 *
 * @param cp Unicode code point.
 * @return The first font in #Font_List containing @p cp, the primary font if
 *         none do, or nullptr if no fonts are loaded.
 */
static XftFont *Pick_Font_For_Codepoint(uint32_t cp)
{
    for (int i = 0; i < Font_Count; i++)
        if (Font_List[i] && XftCharExists(display, Font_List[i], (FcChar32)cp))
            return Font_List[i];

    return Font_Count > 0 ? Font_List[0] : nullptr;
}

/**
 * @brief Draw UTF-8 text with per-character font fallback and advance the pen position.
 *
 * The string is split into runs that use the same font, and each run is drawn
 * with the font that supports it. Passing nullptr for @p draw measures the
 * text without drawing it.
 *
 * @param draw     Xft draw context, or nullptr to only measure.
 * @param color    Text color (unused when only measuring).
 * @param[in,out] x Pen X position; advanced by the width of the text.
 * @param y        Baseline Y position.
 * @param s        UTF-8 text to draw.
 */
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

/**
 * @brief Draw UTF-8 text at a fixed position with font fallback.
 *
 * Same as DrawUtf8Adv() but does not return the advanced X position.
 *
 * @param draw  Xft draw context, or nullptr to do nothing visible.
 * @param color Text color.
 * @param x     Starting X position.
 * @param y     Baseline Y position.
 * @param s     UTF-8 text to draw.
 */
void DrawUtf8(XftDraw *draw, XftColor *color, int x, int y, const string &s)
{
    DrawUtf8Adv(draw, color, x, y, s);
}

/**
 * @brief Shorten text to fit a pixel width, adding an ellipsis if it is cut.
 *
 * Truncation happens on UTF-8 character boundaries.
 *
 * @param text      UTF-8 text to fit.
 * @param max_width Available width in pixels.
 * @return @p text unchanged if it fits, otherwise a prefix followed by "…";
 *         an empty string if @p max_width is not positive.
 */
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

// ============================================================================
// Text selection
//
// Rows are "virtual rows" of a tab: 0..buffer.size()-1 are finished lines, the
// next one is the partial current_line (if any), and the very last one is the
// prompt row (index Content_Line_Count()). Columns are byte offsets into a
// row's plain text.
// ============================================================================

/** @brief A mouse text selection in one tab. */
struct Text_Selection
{
    const Tab *tab = nullptr;           ///< Tab the selection belongs to; nullptr when there is none.
    bool dragging = false;              ///< True while the left mouse button is held.
    int anchor_row = 0, anchor_col = 0; ///< Where the drag started.
    int head_row = 0, head_col = 0;     ///< Where the pointer is now.
    int last_x = 0, last_y = 0;         ///< Last pointer position while dragging, used for auto-scroll.
};

Text_Selection Selection; ///< The current selection (at most one, in the active tab).

/** @brief Remove the current selection. */
void Clear_Selection()
{
    Selection = Text_Selection();
}

/** @brief Whether @p T has a non-empty selection. */
bool Has_Selection(const Tab &T)
{
    return Selection.tab == &T &&
           (Selection.anchor_row != Selection.head_row || Selection.anchor_col != Selection.head_col);
}

/** @brief The selection's start and end, in reading order. */
static void Selection_Range(int &r1, int &c1, int &r2, int &c2)
{
    r1 = Selection.anchor_row, c1 = Selection.anchor_col;
    r2 = Selection.head_row, c2 = Selection.head_col;
    if (r2 < r1 || (r2 == r1 && c2 < c1))
    {
        swap(r1, r2);
        swap(c1, c2);
    }
}

/** @brief Number of output rows of a tab (finished lines plus the partial line), excluding the prompt row. */
int Content_Line_Count(const Tab &T)
{
    return (int)T.buffer.size() + (T.current_line.empty() ? 0 : 1);
}

/**
 * @brief The prompt shown before the input line, with ANSI colors.
 *
 * Empty while a foreground command runs, "Search: " in search mode, "❯ "
 * during autocomplete, "> " while the shell waits for more lines (an unclosed
 * quote), and the Get_Current_Path() prompt otherwise.
 */
string Prompt_Prefix(const Tab &T)
{
    if (T.foreground_busy)
        return "";
    if (T.is_search)
        return "\x1b[200mSearch:\x1b[0m ";
    if (T.is_autocomplete)
        return "\x1b[203m❯\x1b[0m ";
    if (T.continuation)
        return "\x1b[201m>\x1b[0m ";
    return Get_Current_Path(T.child_pid);
}

/** @brief Concatenate the text of a line's segments. */
static string Segments_Text(const vector<Segment> &line)
{
    string out;
    for (auto &seg : line)
        out += seg.text;
    return out;
}

/** @brief Plain text of virtual row @p v (see the section comment above). */
string Row_Text(const Tab &T, int v)
{
    int buffered = (int)T.buffer.size();
    if (v >= 0 && v < buffered)
        return Segments_Text(T.buffer[v]);
    if (v == buffered && !T.current_line.empty())
        return Segments_Text(T.current_line);
    return Segments_Text(AnsiToSegments(Prompt_Prefix(T))) + T.line;
}

/** @brief Pixel X where byte offset @p col of a row's text starts. */
static int Text_X(const string &text, int col)
{
    int x = CONTENT_PAD_X;
    DrawUtf8Adv(nullptr, nullptr, x, 0, text.substr(0, min((size_t)max(col, 0), text.size())));
    return x;
}

/** @brief Byte offset in a row's text closest to pixel X @p px (snaps to character edges). */
static int Col_At_X(const string &text, int px)
{
    int x = CONTENT_PAD_X;
    size_t i = 0;
    while (i < text.size())
    {
        uint32_t cp;
        int len = Utf8_Decode(text, i, cp);
        int w = 0;
        DrawUtf8Adv(nullptr, nullptr, w, 0, text.substr(i, len));
        if (px < x + w / 2)
            return (int)i;
        x += w;
        i += len;
    }
    return (int)text.size();
}

/** @brief Virtual row under pixel Y @p py, clamped to the rows currently on screen. */
static int Row_At_Y(const Tab &T, int py)
{
    int top = Tab_Height + CONTENT_PAD_TOP;
    int offset = py < top ? 0 : (py - top) / Line_Height();
    int last = min(Content_Line_Count(T), T.scroll_index + Visible_Line_Count());
    return min(T.scroll_index + offset, last);
}

/** @brief The selected text, rows joined with newlines. */
string Selected_Text(const Tab &T)
{
    if (!Has_Selection(T))
        return "";

    int r1, c1, r2, c2;
    Selection_Range(r1, c1, r2, c2);

    string out;
    for (int v = r1; v <= r2; v++)
    {
        string text = Row_Text(T, v);
        size_t a = v == r1 ? min((size_t)c1, text.size()) : 0;
        size_t b = v == r2 ? min((size_t)c2, text.size()) : text.size();
        out += text.substr(a, b > a ? b - a : 0);
        if (v != r2)
            out += "\n";
    }
    return out;
}

/**
 * @brief Byte offset of the character drawn under pixel X @p px.
 *
 * @return The start of that character, or text.size() if @p px is past the end.
 */
static int Char_At_X(const string &text, int px)
{
    int x = CONTENT_PAD_X;
    size_t i = 0;
    while (i < text.size())
    {
        uint32_t cp;
        int len = Utf8_Decode(text, i, cp);
        int w = 0;
        DrawUtf8Adv(nullptr, nullptr, w, 0, text.substr(i, len));
        if (px < x + w)
            return (int)i;
        x += w;
        i += len;
    }
    return (int)text.size();
}

/**
 * @brief Character class used by double-click word selection.
 *
 * @return 0 for whitespace, 1 for word characters (letters, digits, non-ASCII
 *         and the path/URL characters "_-./~:@%+=#?&"), 2 for anything else.
 */
static int Char_Class(const string &text, size_t i)
{
    unsigned char c = text[i];
    if (c == ' ' || c == '\t')
        return 0;
    if (c >= 0x80 || isalnum(c) || strchr("_-./~:@%+=#?&", c))
        return 1;
    return 2;
}

/**
 * @brief Where a pointer position lands as a selection end point.
 *
 * Above the text it snaps to the start of the top row, below the last row to
 * the end of that row, otherwise to the nearest character edge.
 */
static void Selection_Point_At(const Tab &T, int px, int py, int &row, int &col)
{
    int top = Tab_Height + CONTENT_PAD_TOP;
    int last = min(Content_Line_Count(T), T.scroll_index + Visible_Line_Count());

    row = Row_At_Y(T, py);
    string text = Row_Text(T, row);
    if (py < top)
        col = 0;
    else if (T.scroll_index + (py - top) / Line_Height() > last)
        col = (int)text.size();
    else
        col = Col_At_X(text, px);
}

/** @brief Start a drag selection at a mouse position in tab @p T. */
void Selection_Begin(const Tab &T, int px, int py)
{
    Selection.tab = &T;
    Selection.dragging = true;
    Selection.last_x = px;
    Selection.last_y = py;
    Selection_Point_At(T, px, py, Selection.anchor_row, Selection.anchor_col);
    Selection.head_row = Selection.anchor_row;
    Selection.head_col = Selection.anchor_col;
}

/** @brief Select the word (or whitespace run, or single symbol) under a mouse position. */
void Selection_Select_Word(const Tab &T, int px, int py)
{
    int row = Row_At_Y(T, py);
    string text = Row_Text(T, row);
    size_t a = Char_At_X(text, px), b = a;

    if (a < text.size())
    {
        int cls = Char_Class(text, a);
        if (cls == 2)
            b = a + 1;
        else
        {
            // Byte-wise is safe: every byte of a UTF-8 character is class 1.
            while (a > 0 && Char_Class(text, a - 1) == cls)
                a--;
            while (b < text.size() && Char_Class(text, b) == cls)
                b++;
        }
    }

    Selection.tab = &T;
    Selection.dragging = false;
    Selection.anchor_row = Selection.head_row = row;
    Selection.anchor_col = (int)a;
    Selection.head_col = (int)b;
}

/** @brief Select the whole row under a mouse position. */
void Selection_Select_Line(const Tab &T, int py)
{
    int row = Row_At_Y(T, py);
    Selection.tab = &T;
    Selection.dragging = false;
    Selection.anchor_row = Selection.head_row = row;
    Selection.anchor_col = 0;
    Selection.head_col = (int)Row_Text(T, row).size();
}

/**
 * @brief Handle a left-button press in the text area.
 *
 * One click starts a drag selection, a quick second click on the same spot
 * selects a word, and a third selects the whole line.
 *
 * @param time Timestamp of the button event, in milliseconds.
 */
void Selection_Click(const Tab &T, int px, int py, Time time)
{
    static Time Last_Time = 0;
    static int Last_X = -100, Last_Y = -100, Click_Count = 0;
    const Time Multi_Click_MS = 400;
    const int Slop = 4;

    bool same_spot = abs(px - Last_X) <= Slop && abs(py - Last_Y) <= Slop;
    Click_Count = (same_spot && time - Last_Time <= Multi_Click_MS) ? Click_Count % 3 + 1 : 1;
    Last_Time = time;
    Last_X = px;
    Last_Y = py;

    if (Click_Count == 2)
        Selection_Select_Word(T, px, py);
    else if (Click_Count == 3)
        Selection_Select_Line(T, py);
    else
        Selection_Begin(T, px, py);
}

/**
 * @brief Move the end of the selection being dragged.
 *
 * @return true if the selection changed and the window needs a redraw.
 */
bool Selection_Extend(const Tab &T, int px, int py)
{
    if (!Selection.dragging || Selection.tab != &T)
        return false;

    Selection.last_x = px;
    Selection.last_y = py;

    int row, col;
    Selection_Point_At(T, px, py, row, col);
    if (row == Selection.head_row && col == Selection.head_col)
        return false;

    Selection.head_row = row;
    Selection.head_col = col;
    return true;
}

/**
 * @brief Scroll while a drag is held above or below the text, extending the selection.
 *
 * Called on every main-loop tick, so it keeps scrolling even when the mouse
 * stops moving. The further outside the text the pointer is, the faster it goes.
 *
 * @return true if the view scrolled and the window needs a redraw.
 */
bool Selection_Autoscroll(Tab &T)
{
    if (!Selection.dragging || Selection.tab != &T)
        return false;

    int lh = Line_Height();
    int top = Tab_Height + CONTENT_PAD_TOP;
    int bottom = top + (Visible_Line_Count() + 1) * lh;
    int y = Selection.last_y;

    int step = 0;
    if (y < top)
        step = -(1 + (top - y) / lh);
    else if (y >= bottom)
        step = 1 + (y - bottom) / lh;
    if (step == 0)
        return false;

    int max_scroll = max(0, Content_Line_Count(T) - Visible_Line_Count());
    int new_scroll = max(0, min(max_scroll, T.scroll_index + step));
    if (new_scroll == T.scroll_index)
        return false;

    T.scroll_index = new_scroll;
    Selection_Point_At(T, Selection.last_x, y, Selection.head_row, Selection.head_col);
    return true;
}

/** @brief Mix two 0xRRGGBB colors; @p t = 0 gives @p a, 1 gives @p b. */
static unsigned long Blend_Rgb(unsigned long a, unsigned long b, double t)
{
    unsigned long out = 0;
    for (int shift = 0; shift <= 16; shift += 8)
    {
        int ca = (a >> shift) & 0xff, cb = (b >> shift) & 0xff;
        out |= (unsigned long)(ca + (cb - ca) * t + 0.5) << shift;
    }
    return out;
}

/**
 * @brief Redraw the whole window.
 *
 * Draws the tab bar (flat tabs, the active one underlined in the theme accent
 * with its close button, plus a borderless "+"), the visible part of the active
 * tab's scrollback with the mouse selection highlighted, and the prompt line
 * (see Prompt_Prefix()) with the input text and a block cursor. The prompt is
 * the last scrollback row, so it is hidden while the view is scrolled up.
 * All colors come from Current_Theme().
 */
void Draw_UI()
{
    const Theme &th = Current_Theme();
    XClearWindow(display, window);

    // ---- Tab bar: flat strip, active tab blends into the content with an accent underline ----
    XSetForeground(display, gc, th.tabbar_bg);
    XFillRectangle(display, window, gc, 0, 0, Win_Width, Tab_Height);

    XSetForeground(display, gc, th.border);
    XFillRectangle(display, window, gc, 0, Tab_Height - 1, Win_Width, 1);

    int font_ascent = Xft_Font ? Xft_Font->ascent : 12;
    int font_descent = Xft_Font ? Xft_Font->descent : 6;
    int Title_Baseline = (Tab_Height + font_ascent - font_descent) / 2;

    int Tab_X = 0;
    for (size_t i = 0; i < Tabs.size(); i++)
    {
        bool active = (int)i == Active_Tab;
        int Tab_W = TAB_WIDTH;

        if (active)
        {
            XSetForeground(display, gc, th.bg);
            XFillRectangle(display, window, gc, Tab_X, 0, Tab_W, Tab_Height);
            XSetForeground(display, gc, th.accent);
            XFillRectangle(display, window, gc, Tab_X, Tab_Height - ACCENT_THICKNESS, Tab_W, ACCENT_THICKNESS);
        }
        else
        {
            // Short divider on the right, unless the next tab is the active one.
            bool next_active = (int)i + 1 == Active_Tab;
            if (!next_active)
            {
                XSetForeground(display, gc, th.border);
                XFillRectangle(display, window, gc, Tab_X + Tab_W - 1, Tab_Height / 3, 1, Tab_Height / 3);
            }
        }

        int Close_X = Tab_X + Tab_W - CLOSE_BOX_W - CLOSE_BOX_MARGIN;
        int Close_Y = (Tab_Height - CLOSE_BOX_H) / 2;

        int Title_Right = active ? Close_X - 6 : Tab_X + Tab_W - TAB_PADDING_L;
        string Tab_Title = Truncate_To_Width(Tabs[i]->title, Title_Right - (Tab_X + TAB_PADDING_L));
        DrawUtf8(Xft_Draw, active ? &Xft_Color : &Xft_Color_Dim, Tab_X + TAB_PADDING_L, Title_Baseline, Tab_Title);

        // The close button is only shown on the active tab.
        if (active)
        {
            XSetForeground(display, gc, th.subtext);
            XSetLineAttributes(display, gc, 2, LineSolid, CapRound, JoinRound);
            int Close_Pad = 4;
            XDrawLine(display, window, gc, Close_X + Close_Pad, Close_Y + Close_Pad, Close_X + CLOSE_BOX_W - Close_Pad, Close_Y + CLOSE_BOX_H - Close_Pad);
            XDrawLine(display, window, gc, Close_X + CLOSE_BOX_W - Close_Pad, Close_Y + Close_Pad, Close_X + Close_Pad, Close_Y + CLOSE_BOX_H - Close_Pad);
        }

        Tab_X += Tab_W;
    }

    // Borderless "+" button.
    int Plus_X = Tab_X + 8;
    int Plus_Y = (Tab_Height - PLUS_BOX_H) / 2;
    XSetForeground(display, gc, th.subtext);
    XSetLineAttributes(display, gc, 2, LineSolid, CapRound, JoinRound);
    int Plus_Cx = Plus_X + PLUS_BOX_W / 2, Plus_Cy = Plus_Y + PLUS_BOX_H / 2, Plus_Arm = PLUS_BOX_W / 2 - 5;
    XDrawLine(display, window, gc, Plus_Cx - Plus_Arm, Plus_Cy, Plus_Cx + Plus_Arm, Plus_Cy);
    XDrawLine(display, window, gc, Plus_Cx, Plus_Cy - Plus_Arm, Plus_Cx, Plus_Cy + Plus_Arm);
    XSetLineAttributes(display, gc, 1, LineSolid, CapButt, JoinMiter);

    if (Tabs.empty())
    {
        DrawUtf8(Xft_Draw, &Xft_Color_Dim, CONTENT_PAD_X, Tab_Height + CONTENT_PAD_TOP + font_ascent, "No tabs, press Ctrl+T to open one");
        XFlush(display);
        return;
    }

    // ---- Scrollback and prompt ----
    // The prompt is the last virtual row, so scrolling up moves it off the
    // bottom of the window like any other line.
    Tab &T = *Tabs[Active_Tab];
    int line_height = Line_Height();
    int Total_Lines = Content_Line_Count(T);
    int Max_Lines_Visible = Visible_Line_Count();

    if (T.scroll_index > Total_Lines - Max_Lines_Visible)
        T.scroll_index = max(0, Total_Lines - Max_Lines_Visible);

    int Last_Row = min(Total_Lines, T.scroll_index + Max_Lines_Visible);
    bool Prompt_Visible = Last_Row == Total_Lines;

    bool Show_Selection = Has_Selection(T);
    int Sel_R1 = 0, Sel_C1 = 0, Sel_R2 = -1, Sel_C2 = 0;
    if (Show_Selection)
        Selection_Range(Sel_R1, Sel_C1, Sel_R2, Sel_C2);
    unsigned long Selection_Color = Blend_Rgb(th.bg, th.accent, 0.35);

    // Baseline of the first row; LINE_GAP is split above and below the glyphs.
    int First_Baseline = Tab_Height + CONTENT_PAD_TOP + LINE_GAP / 2 + font_ascent;
    int Y = First_Baseline;

    for (int v = T.scroll_index; v <= Last_Row && Y - font_ascent < Win_Height; v++, Y += line_height)
    {
        if (Show_Selection && v >= Sel_R1 && v <= Sel_R2)
        {
            string text = Row_Text(T, v);
            int a = v == Sel_R1 ? min(Sel_C1, (int)text.size()) : 0;
            int b = v == Sel_R2 ? min(Sel_C2, (int)text.size()) : (int)text.size();
            int x1 = Text_X(text, a), x2 = Text_X(text, b);
            if (v != Sel_R2)
                x2 += max(1, font_ascent / 2); // show the line break as a little extra
            XSetForeground(display, gc, Selection_Color);
            XFillRectangle(display, window, gc, x1, Y - font_ascent - LINE_GAP / 2, max(0, x2 - x1), line_height);
        }

        if (v == Total_Lines)
            break; // the prompt row is drawn below

        const vector<Segment> &Line = (v < (int)T.buffer.size()) ? T.buffer[v] : T.current_line;

        int x = CONTENT_PAD_X;
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
    }

    if (!Prompt_Visible)
    {
        XFlush(display);
        return;
    }

    // ---- Prompt line (Y is its baseline after the loop's break) ----
    vector<Segment> Prefix_Segments = AnsiToSegments(Prompt_Prefix(T));

    int Prompt_X = CONTENT_PAD_X;
    for (auto &seg : Prefix_Segments)
        DrawUtf8Adv(Xft_Draw, &Ansi_Palette[(int)seg.color], Prompt_X, Y, seg.text);
    DrawUtf8Adv(Xft_Draw, &Xft_Color, Prompt_X, Y, T.line);

    if (T.is_autocomplete)
    {
        XFlush(display);
        return;
    }

    // ---- Block cursor ----
    size_t Cursor_Byte = min((size_t)T.writing_index, T.line.size());
    int Cursor_X = CONTENT_PAD_X;
    for (auto &seg : Prefix_Segments)
        DrawUtf8Adv(nullptr, nullptr, Cursor_X, Y, seg.text);
    DrawUtf8Adv(nullptr, nullptr, Cursor_X, Y, T.line.substr(0, Cursor_Byte));

    // The character under the cursor (a space at the end of the line).
    string Under_Cursor = " ";
    if (Cursor_Byte < T.line.size())
    {
        uint32_t cp;
        Under_Cursor = T.line.substr(Cursor_Byte, Utf8_Decode(T.line, Cursor_Byte, cp));
    }
    int Cursor_W = 0;
    DrawUtf8Adv(nullptr, nullptr, Cursor_W, Y, Under_Cursor);
    if (Cursor_W <= 0)
        Cursor_W = max(1, font_ascent / 2);

    int Cursor_Y = Y - font_ascent;
    int Cursor_H = font_ascent + font_descent;

    XSetForeground(display, gc, th.cursor);
    if (Cursor_Visible)
    {
        // Solid block with the character drawn in the background color.
        XFillRectangle(display, window, gc, Cursor_X, Cursor_Y, Cursor_W, Cursor_H);
        if (Under_Cursor != " ")
            DrawUtf8(Xft_Draw, &Xft_Color_Bg, Cursor_X, Y, Under_Cursor);
    }
    else
    {
        // Off phase of the blink: a hollow outline, so the cursor never disappears.
        XDrawRectangle(display, window, gc, Cursor_X, Cursor_Y, Cursor_W - 1, Cursor_H - 1);
    }

    XFlush(display);
}
