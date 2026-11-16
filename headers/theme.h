#pragma once

#include <string>
#include <vector>
#include <cctype>
#include <cstdlib>
#include <fstream>

using namespace std;

/**
 * @brief A complete color theme: window chrome, prompt colors and the 16 ANSI text colors.
 *
 * All colors are 0xRRGGBB.
 */
struct Theme
{
    const char *name;           ///< Name used by the /theme command.
    unsigned long bg;           ///< Terminal background, also the active tab's background.
    unsigned long tabbar_bg;    ///< Tab bar background.
    unsigned long border;       ///< Tab bar bottom border and dividers between tabs.
    unsigned long dim;          ///< Inactive tab titles, hints and the parent part of the prompt path.
    unsigned long subtext;      ///< Close and "+" icons.
    unsigned long fg;           ///< Default text color.
    unsigned long accent;       ///< Active tab underline and window icon glyph.
    unsigned long cursor;       ///< Cursor color.
    unsigned long prompt_host;  ///< "user@host" in the prompt.
    unsigned long prompt_dir;   ///< Current folder in the prompt.
    unsigned long prompt_arrow; ///< The "❯" at the end of the prompt.
    unsigned long ansi[16];     ///< Black, Red, Green, Yellow, Blue, Magenta, Cyan, White, then the bright variants.
};

/** @brief Built-in themes. The first one is the default. */
const Theme THEMES[] = {
    {"catppuccin-mocha",
     0x1e1e2e,
     0x181825,
     0x313244,
     0x6c7086,
     0xa6adc8,
     0xcdd6f4,
     0xcba6f7,
     0xf5e0dc,
     0xcba6f7,
     0x89b4fa,
     0xa6e3a1,
     {0x45475a, 0xf38ba8, 0xa6e3a1, 0xf9e2af, 0x89b4fa, 0xf5c2e7, 0x94e2d5, 0xbac2de,
      0x585b70, 0xf38ba8, 0xa6e3a1, 0xf9e2af, 0x89b4fa, 0xf5c2e7, 0x94e2d5, 0xa6adc8}},
    {"catppuccin-macchiato",
     0x24273a,
     0x1e2030,
     0x363a4f,
     0x6e738d,
     0xa5adcb,
     0xcad3f5,
     0xc6a0f6,
     0xf4dbd6,
     0xc6a0f6,
     0x8aadf4,
     0xa6da95,
     {0x494d64, 0xed8796, 0xa6da95, 0xeed49f, 0x8aadf4, 0xf5bde6, 0x8bd5ca, 0xb8c0e0,
      0x5b6078, 0xed8796, 0xa6da95, 0xeed49f, 0x8aadf4, 0xf5bde6, 0x8bd5ca, 0xa5adcb}},
    {"catppuccin-frappe",
     0x303446,
     0x292c3c,
     0x414559,
     0x737994,
     0xa5adce,
     0xc6d0f5,
     0xca9ee6,
     0xf2d5cf,
     0xca9ee6,
     0x8caaee,
     0xa6d189,
     {0x51576d, 0xe78284, 0xa6d189, 0xe5c890, 0x8caaee, 0xf4b8e4, 0x81c8be, 0xb5bfe2,
      0x626880, 0xe78284, 0xa6d189, 0xe5c890, 0x8caaee, 0xf4b8e4, 0x81c8be, 0xa5adce}},
    {"catppuccin-latte",
     0xeff1f5,
     0xe6e9ef,
     0xccd0da,
     0x8c8fa1,
     0x6c6f85,
     0x4c4f69,
     0x8839ef,
     0xdc8a78,
     0x8839ef,
     0x1e66f5,
     0x40a02b,
     {0x5c5f77, 0xd20f39, 0x40a02b, 0xdf8e1d, 0x1e66f5, 0xea76cb, 0x179299, 0xacb0be,
      0x6c6f85, 0xd20f39, 0x40a02b, 0xdf8e1d, 0x1e66f5, 0xea76cb, 0x179299, 0xbcc0cc}},
    {"tokyo-night",
     0x1a1b26,
     0x16161e,
     0x292e42,
     0x565f89,
     0xa9b1d6,
     0xc0caf5,
     0x7aa2f7,
     0xc0caf5,
     0xbb9af7,
     0x7aa2f7,
     0x9ece6a,
     {0x15161e, 0xf7768e, 0x9ece6a, 0xe0af68, 0x7aa2f7, 0xbb9af7, 0x7dcfff, 0xa9b1d6,
      0x414868, 0xf7768e, 0x9ece6a, 0xe0af68, 0x7aa2f7, 0xbb9af7, 0x7dcfff, 0xc0caf5}},
    {"tokyo-night-storm",
     0x24283b,
     0x1f2335,
     0x3b4261,
     0x565f89,
     0xa9b1d6,
     0xc0caf5,
     0x7aa2f7,
     0xc0caf5,
     0xbb9af7,
     0x7aa2f7,
     0x9ece6a,
     {0x1d202f, 0xf7768e, 0x9ece6a, 0xe0af68, 0x7aa2f7, 0xbb9af7, 0x7dcfff, 0xa9b1d6,
      0x414868, 0xf7768e, 0x9ece6a, 0xe0af68, 0x7aa2f7, 0xbb9af7, 0x7dcfff, 0xc0caf5}},
    {"rose-pine",
     0x191724,
     0x1f1d2e,
     0x26233a,
     0x6e6a86,
     0x908caa,
     0xe0def4,
     0xebbcba,
     0xe0def4,
     0xc4a7e7,
     0x9ccfd8,
     0xf6c177,
     {0x26233a, 0xeb6f92, 0x31748f, 0xf6c177, 0x9ccfd8, 0xc4a7e7, 0xebbcba, 0xe0def4,
      0x6e6a86, 0xeb6f92, 0x31748f, 0xf6c177, 0x9ccfd8, 0xc4a7e7, 0xebbcba, 0xe0def4}},
    {"rose-pine-moon",
     0x232136,
     0x2a273f,
     0x393552,
     0x6e6a86,
     0x908caa,
     0xe0def4,
     0xea9a97,
     0xe0def4,
     0xc4a7e7,
     0x9ccfd8,
     0xf6c177,
     {0x393552, 0xeb6f92, 0x3e8fb0, 0xf6c177, 0x9ccfd8, 0xc4a7e7, 0xea9a97, 0xe0def4,
      0x6e6a86, 0xeb6f92, 0x3e8fb0, 0xf6c177, 0x9ccfd8, 0xc4a7e7, 0xea9a97, 0xe0def4}},
    {"rose-pine-dawn",
     0xfaf4ed,
     0xf2e9e1,
     0xdfdad9,
     0x9893a5,
     0x797593,
     0x575279,
     0xd7827e,
     0x575279,
     0x907aa9,
     0x286983,
     0xd7827e,
     {0xf2e9e1, 0xb4637a, 0x286983, 0xea9d34, 0x56949f, 0x907aa9, 0xd7827e, 0x575279,
      0x9893a5, 0xb4637a, 0x286983, 0xea9d34, 0x56949f, 0x907aa9, 0xd7827e, 0x575279}},
    {"nord",
     0x2e3440,
     0x272c36,
     0x3b4252,
     0x616e88,
     0xd8dee9,
     0xd8dee9,
     0x88c0d0,
     0xd8dee9,
     0xb48ead,
     0x88c0d0,
     0xa3be8c,
     {0x3b4252, 0xbf616a, 0xa3be8c, 0xebcb8b, 0x81a1c1, 0xb48ead, 0x88c0d0, 0xe5e9f0,
      0x4c566a, 0xbf616a, 0xa3be8c, 0xebcb8b, 0x81a1c1, 0xb48ead, 0x8fbcbb, 0xeceff4}},
    {"dracula",
     0x282a36,
     0x21222c,
     0x44475a,
     0x6272a4,
     0xa4a6b6,
     0xf8f8f2,
     0xbd93f9,
     0xf8f8f2,
     0xff79c6,
     0xbd93f9,
     0x50fa7b,
     {0x21222c, 0xff5555, 0x50fa7b, 0xf1fa8c, 0xbd93f9, 0xff79c6, 0x8be9fd, 0xf8f8f2,
      0x6272a4, 0xff6e6e, 0x69ff94, 0xffffa5, 0xd6acff, 0xff92df, 0xa4ffff, 0xffffff}},
    {"gruvbox-dark",
     0x282828,
     0x1d2021,
     0x3c3836,
     0x7c6f64,
     0xa89984,
     0xebdbb2,
     0xfe8019,
     0xebdbb2,
     0xd3869b,
     0x83a598,
     0xb8bb26,
     {0x282828, 0xcc241d, 0x98971a, 0xd79921, 0x458588, 0xb16286, 0x689d6a, 0xa89984,
      0x928374, 0xfb4934, 0xb8bb26, 0xfabd2f, 0x83a598, 0xd3869b, 0x8ec07c, 0xebdbb2}},
    {"one-dark",
     0x282c34,
     0x21252b,
     0x3e4451,
     0x5c6370,
     0x9da5b4,
     0xabb2bf,
     0x61afef,
     0x528bff,
     0xc678dd,
     0x61afef,
     0x98c379,
     {0x282c34, 0xe06c75, 0x98c379, 0xe5c07b, 0x61afef, 0xc678dd, 0x56b6c2, 0xabb2bf,
      0x5c6370, 0xe06c75, 0x98c379, 0xe5c07b, 0x61afef, 0xc678dd, 0x56b6c2, 0xffffff}},
    {"kanagawa",
     0x1f1f28,
     0x16161d,
     0x363646,
     0x727169,
     0xc8c093,
     0xdcd7ba,
     0x7e9cd8,
     0xc8c093,
     0x957fb8,
     0x7fb4ca,
     0x98bb6c,
     {0x090618, 0xc34043, 0x76946a, 0xc0a36e, 0x7e9cd8, 0x957fb8, 0x6a9589, 0xc8c093,
      0x727169, 0xe82424, 0x98bb6c, 0xe6c384, 0x7fb4ca, 0x938aa9, 0x7aa89f, 0xdcd7ba}},
    {"everforest",
     0x2d353b,
     0x232a2e,
     0x3d484d,
     0x859289,
     0x9da9a0,
     0xd3c6aa,
     0xa7c080,
     0xd3c6aa,
     0xd699b6,
     0x7fbbb3,
     0xa7c080,
     {0x4f585e, 0xe67e80, 0xa7c080, 0xdbbc7f, 0x7fbbb3, 0xd699b6, 0x83c092, 0xd3c6aa,
      0x7a8478, 0xe67e80, 0xa7c080, 0xdbbc7f, 0x7fbbb3, 0xd699b6, 0x83c092, 0xd3c6aa}},
    {"github-dark",
     0x0d1117,
     0x010409,
     0x30363d,
     0x6e7681,
     0x8b949e,
     0xe6edf3,
     0x2f81f7,
     0xe6edf3,
     0xbc8cff,
     0x58a6ff,
     0x3fb950,
     {0x484f58, 0xff7b72, 0x3fb950, 0xd29922, 0x58a6ff, 0xbc8cff, 0x39c5cf, 0xb1bac4,
      0x6e7681, 0xffa198, 0x56d364, 0xe3b341, 0x79c0ff, 0xd2a8ff, 0x56d4dd, 0xffffff}},
    {"solarized-dark",
     0x002b36,
     0x002129,
     0x073642,
     0x586e75,
     0x93a1a1,
     0x839496,
     0x268bd2,
     0x93a1a1,
     0xd33682,
     0x268bd2,
     0x859900,
     {0x073642, 0xdc322f, 0x859900, 0xb58900, 0x268bd2, 0xd33682, 0x2aa198, 0xeee8d5,
      0x586e75, 0xcb4b16, 0x859900, 0xb58900, 0x268bd2, 0x6c71c4, 0x2aa198, 0xfdf6e3}},
    {"ayu-mirage",
     0x1f2430,
     0x191e2a,
     0x33415e,
     0x707a8c,
     0x8a9199,
     0xcccac2,
     0xffcc66,
     0xffcc66,
     0xdabafa,
     0x73d0ff,
     0x87d96c,
     {0x171b24, 0xed8274, 0x87d96c, 0xfacc6e, 0x6dcbfa, 0xdabafa, 0x90e1c6, 0xc7c7c7,
      0x686868, 0xf28779, 0xd5ff80, 0xffd173, 0x73d0ff, 0xdfbfff, 0x95e6cb, 0xffffff}},
    {"night-owl",
     0x011627,
     0x010e1a,
     0x1d3b53,
     0x5f7e97,
     0x7e97ac,
     0xd6deeb,
     0x82aaff,
     0x80a4c2,
     0xc792ea,
     0x82aaff,
     0x22da6e,
     {0x011627, 0xef5350, 0x22da6e, 0xaddb67, 0x82aaff, 0xc792ea, 0x21c7a8, 0xffffff,
      0x575656, 0xef5350, 0x22da6e, 0xffeb95, 0x82aaff, 0xc792ea, 0x7fdbca, 0xffffff}},
};

int Current_Theme_Index = 0;                                ///< Index into #THEMES of the active theme.
const int THEME_COUNT = sizeof(THEMES) / sizeof(THEMES[0]); ///< Number of built-in themes.

/** @brief The active theme. */
inline const Theme &Current_Theme()
{
    return THEMES[Current_Theme_Index];
}

/** @brief Whether a theme has a light background (used to label it in /theme). */
inline bool Is_Light_Theme(const Theme &th)
{
    int r = (th.bg >> 16) & 0xff, g = (th.bg >> 8) & 0xff, b = th.bg & 0xff;
    return (r * 299 + g * 587 + b * 114) / 1000 > 128;
}

/**
 * @brief Lowercase a name and drop everything but letters and digits.
 *
 * Lets "Tokyo Night", "tokyo-night" and "tokyonight" all match.
 */
static string Normalize_Theme_Name(const string &s)
{
    string out;
    for (unsigned char c : s)
        if (isalnum(c))
            out += (char)tolower(c);
    return out;
}

/**
 * @brief Find the themes a name could refer to.
 *
 * An exact (normalized) match returns just that theme; otherwise every theme
 * the name is a prefix of is returned, so "gruv" finds gruvbox-dark.
 *
 * @return Indices into #THEMES; empty if nothing matches.
 */
vector<int> Theme_Matches(const string &name)
{
    vector<int> matches;
    string key = Normalize_Theme_Name(name);
    if (key.empty())
        return matches;

    for (int i = 0; i < THEME_COUNT; i++)
    {
        string candidate = Normalize_Theme_Name(THEMES[i].name);
        if (candidate == key)
            return {i};
        if (candidate.compare(0, key.size(), key) == 0)
            matches.push_back(i);
    }

    return matches;
}

/**
 * @brief Find a theme by name (exact name or a unique prefix).
 *
 * @return The theme's index in #THEMES, or -1 if none or several match.
 */
int Find_Theme(const string &name)
{
    vector<int> matches = Theme_Matches(name);
    return matches.size() == 1 ? matches[0] : -1;
}

/** @brief Path of the file that remembers the chosen theme (~/.terminal_theme). */
static string Theme_File_Path()
{
    const char *home = getenv("HOME");
    return string(home ? home : ".") + "/.terminal_theme";
}

/** @brief Read the saved theme name, if any, into #Current_Theme_Index. */
void Load_Saved_Theme()
{
    ifstream in(Theme_File_Path());
    string name;
    if (in && getline(in, name))
    {
        int idx = Find_Theme(name);
        if (idx >= 0)
            Current_Theme_Index = idx;
    }
}

/** @brief Remember the active theme for the next start. */
void Save_Theme()
{
    ofstream out(Theme_File_Path(), ios::trunc);
    if (out)
        out << Current_Theme().name << "\n";
}
