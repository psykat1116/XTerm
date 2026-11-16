#pragma once
#include <X11/Xlib.h>

/** @brief Graphics context used for all Xlib drawing (tab bar, lines, cursor). */
GC gc;

/** @brief Default X screen number of #display. */
int screen;

/** @brief The main terminal window. */
Window window;

/** @brief Connection to the X server; nullptr until XOpenDisplay() succeeds. */
Display *display = nullptr;

/** @brief Atom for the PRIMARY selection, used as the paste fallback. */
Atom primaryAtom;

/** @brief Atom of the window property that receives converted selection data. */
Atom propertyAtom;

/** @brief Atom for the CLIPBOARD selection, used for Ctrl-V paste. */
Atom clipboardAtom;

/** @brief Atom for the UTF8_STRING target requested when pasting. */
Atom utf8StringAtom;

int Tab_Height = 36;                 ///< Height of the tab bar in pixels.
bool Cursor_Visible = true;          ///< Current cursor blink state.
int Win_Width = 800, Win_Height = 800; ///< Current window size in pixels, updated on resize.

const int TAB_WIDTH = 170;       ///< Width of each tab in the tab bar, in pixels.
const int PLUS_BOX_W = 20;       ///< Width of the "+" (new tab) button's click area.
const int PLUS_BOX_H = 20;       ///< Height of the "+" (new tab) button's click area.
const int CLOSE_BOX_W = 14;      ///< Width of a tab's close ("x") button.
const int CLOSE_BOX_H = 14;      ///< Height of a tab's close ("x") button.
const int CLOSE_BOX_MARGIN = 10; ///< Gap between the close button and the tab's right edge.
const int TAB_PADDING_L = 14;    ///< Left padding before a tab's title.
const int ACCENT_THICKNESS = 2;  ///< Height of the active tab's accent underline.

const int CONTENT_PAD_X = 12;      ///< Left padding of the terminal text.
const int CONTENT_PAD_TOP = 10;    ///< Gap between the tab bar and the first text line.
const int CONTENT_PAD_BOTTOM = 10; ///< Gap below the last text line.
const int LINE_GAP = 4;            ///< Extra vertical space between text lines.
