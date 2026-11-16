#pragma once
#include "tab.h"
#include <string>
#include <vector>

using namespace std;

int ParseInt(const string &s);
vector<Segment> AnsiToSegments(const string &s);
AnsiColor EffectiveColor(AnsiColor color, bool bold);
void ApplySGR(AnsiColor &color, bool &bold, const string &params);

/**
 * @brief Parse a decimal integer from a string, ignoring non-digit characters.
 *
 * Used for SGR parameter tokens. An empty or digit-free string yields 0.
 *
 * @param s String containing the decimal digits.
 * @return The parsed non-negative integer value.
 */
int ParseInt(const string &s) {
    int val = 0;
    for (char c : s)
        if (c >= '0' && c <= '9')
            val = val * 10 + (c - '0');

    return val;
}

/**
 * @brief Apply an SGR (Select Graphic Rendition) parameter list to the current text style.
 *
 * Handles reset (0), bold on/off (1/22), default foreground (39), the
 * standard foreground colors (30-37), the bright foreground colors (90-97) and
 * the private codes 200-203, which this terminal's own prompt uses to pick the
 * theme's prompt colors. Other codes are ignored. An empty parameter list is treated as a reset.
 *
 * @param[in,out] color  Current foreground color, updated in place.
 * @param[in,out] bold   Current bold flag, updated in place.
 * @param[in]     params Semicolon-separated SGR parameters (the text between "ESC[" and "m").
 */
void ApplySGR(AnsiColor &color, bool &bold, const string &params)
{
    if (params.empty())
    {
        color = AnsiColor::Default;
        bold = false;
        return;
    }

    size_t start = 0;
    while (start <= params.size())
    {
        size_t sep = params.find(';', start);
        string tok = params.substr(start, sep == string::npos ? string::npos : sep - start);
        int code = ParseInt(tok);
        if (code == 0)
        {
            color = AnsiColor::Default;
            bold = false;
        }
        else if (code == 1)
            bold = true;
        else if (code == 22)
            bold = false;
        else if (code == 39)
            color = AnsiColor::Default;
        else if (code >= 30 && code <= 37)
            color = (AnsiColor)(1 + (code - 30));
        else if (code >= 90 && code <= 97)
            color = (AnsiColor)(9 + (code - 90));
        else if (code >= 200 && code <= 203)
            color = (AnsiColor)((int)AnsiColor::PromptHost + (code - 200));

        if (sep == string::npos)
            break;
        start = sep + 1;
    }
}

/**
 * @brief Resolve the color actually drawn for a given color and bold state.
 *
 * Bold text is rendered with the bright variant of the eight standard colors,
 * and bold default text is rendered as bright white.
 *
 * @param color Base foreground color.
 * @param bold  Whether bold is active.
 * @return The color to draw with.
 */
AnsiColor EffectiveColor(AnsiColor color, bool bold)
{
    if (!bold)
        return color;
    if (color == AnsiColor::Default)
        return AnsiColor::BrightWhite;

    int idx = (int)color;
    if (idx >= (int)AnsiColor::Black && idx <= (int)AnsiColor::White)
        return (AnsiColor)(idx + 8);

    return color;
}

/**
 * @brief Split a string containing ANSI escape sequences into colored segments.
 *
 * CSI sequences ("ESC[ ... final-byte") are stripped; those ending in 'm' are
 * applied as SGR styling. Adjacent runs with the same effective color are
 * merged into one segment. An unterminated CSI sequence is kept as literal text.
 *
 * @param s Input text that may contain ANSI escape sequences.
 * @return Ordered list of segments, each holding plain text and its color.
 */
vector<Segment> AnsiToSegments(const string &s)
{
    vector<Segment> segments;
    AnsiColor color = AnsiColor::Default;

    string run;
    bool bold = false;
    size_t i = 0, n = s.size();
    auto flush = [&]()
    {
        if (run.empty())
            return;

        AnsiColor eff = EffectiveColor(color, bold);
        if (!segments.empty() && segments.back().color == eff)
            segments.back().text += run;
        else
            segments.push_back({run, eff});

        run.clear();
    };

    while (i < n)
    {
        unsigned char c = s[i];
        if (c == '\x1b' && i + 1 < n && s[i + 1] == '[')
        {
            size_t j = i + 2;
            while (j < n && !(s[j] >= 0x40 && s[j] <= 0x7e))
                j++;

            if (j >= n) {
                i++;
                run += (char)c;
                continue;
            }

            string params = s.substr(i + 2, j - (i + 2));
            flush();

            if (s[j] == 'm')
                ApplySGR(color, bold, params);

            i = j + 1;
        }
        else
        {
            run += (char)c;
            i++;
        }
    }

    flush();
    return segments;
}
