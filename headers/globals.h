#pragma once
#include <X11/Xlib.h>

GC gc;

int screen;

Window window;

Display *display = nullptr;

Atom primaryAtom;

Atom propertyAtom;

Atom clipboardAtom;

Atom utf8StringAtom;

int Tab_Height = 28;
bool Cursor_Visible = true;
int Win_Width = 800, Win_Height = 800;

const int TAB_WIDTH = 160;
const int PLUS_BOX_W = 18;
const int PLUS_BOX_H = 18;
const int CLOSE_BOX_W = 14;
const int CLOSE_BOX_H = 14;
const int TAB_PADDING_L = 8;
const int TAB_PADDING_TOP = 6;

const unsigned long COLOR_TABBAR_BG = 0xFF181825;
const unsigned long COLOR_TAB_INACTIVE = 0xFF181825;
const unsigned long COLOR_TAB_ACTIVE = 0xFF313244;
const unsigned long COLOR_ACCENT = 0xFF89DCEB;
const unsigned long COLOR_ICON = 0xFFA6ADC8;
const unsigned long COLOR_PLUS_BORDER = 0xFF45475A;
