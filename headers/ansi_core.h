#pragma once
#include "tab.h"
#include <string>
#include <vector>

using namespace std;

int ParseInt(const string &s);
vector<Segment> AnsiToSegments(const string &s);
AnsiColor EffectiveColor(AnsiColor color, bool bold);
void ApplySGR(AnsiColor &color, bool &bold, const string &params);

// Convert string number to integer number
int ParseInt(const string &s) {
    int val = 0;
    for (char c : s)
        if (c >= '0' && c <= '9')
            val = val * 10 + (c - '0');

    return val;
}

// Applying Color to the text segmant
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

        if (sep == string::npos)
            break;
        start = sep + 1;
    }
}

// Converting normal color to deeper shades
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

// Converting string to proper segment format for proper color rendering
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
