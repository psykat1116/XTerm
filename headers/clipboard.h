#pragma once

#include <string>

#include <climits>
#include <iostream>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "tab.h"
#include "render.h"
#include "globals.h"

using namespace std;

string Primary_Text;   ///< Text served for the PRIMARY selection (the last mouse selection).
string Clipboard_Text; ///< Text served for CLIPBOARD (the last Ctrl+Shift+C).

/**
 * @brief Ask the owner of a selection to send us its text as UTF-8.
 *
 * The answer arrives later as a SelectionNotify event, handled by ProcessPasteData().
 *
 * @param selection #clipboardAtom (Ctrl+Shift+V / Ctrl+V) or #primaryAtom (middle click).
 * @param time      Timestamp of the key or button event that triggered the paste.
 */
void Request_Paste(Atom selection, Time time)
{
    XConvertSelection(display, selection, utf8StringAtom, propertyAtom, window, time);
    XFlush(display);
}

/**
 * @brief Finish a mouse selection: publish it as PRIMARY, or drop it if empty.
 */
void Selection_End()
{
    Selection.dragging = false;
    if (Tabs.empty() || !Has_Selection(*Tabs[Active_Tab]))
    {
        Clear_Selection();
        return;
    }

    Primary_Text = Selected_Text(*Tabs[Active_Tab]);
    XSetSelectionOwner(display, primaryAtom, window, CurrentTime);
}

/**
 * @brief Copy the active tab's selection to CLIPBOARD (Ctrl+Shift+C).
 *
 * @return false if nothing is selected.
 */
bool Copy_Selection_To_Clipboard()
{
    if (Tabs.empty() || !Has_Selection(*Tabs[Active_Tab]))
        return false;

    Clipboard_Text = Selected_Text(*Tabs[Active_Tab]);
    XSetSelectionOwner(display, clipboardAtom, window, CurrentTime);
    return XGetSelectionOwner(display, clipboardAtom) == window;
}

/**
 * @brief Answer another application (or ourselves) asking for our selection's text.
 *
 * Supports the TARGETS, UTF8_STRING and STRING targets for PRIMARY and
 * CLIPBOARD; anything else is refused.
 *
 * @param req The SelectionRequest event.
 */
void Handle_Selection_Request(XSelectionRequestEvent *req)
{
    XSelectionEvent reply = {};
    reply.type = SelectionNotify;
    reply.display = req->display;
    reply.requestor = req->requestor;
    reply.selection = req->selection;
    reply.target = req->target;
    reply.time = req->time;
    reply.property = None;

    // Old clients may leave the property empty; the target is used instead.
    Atom property = req->property != None ? req->property : req->target;
    const string *text = req->selection == clipboardAtom ? &Clipboard_Text
                       : req->selection == primaryAtom   ? &Primary_Text
                                                         : nullptr;
    Atom targetsAtom = XInternAtom(display, "TARGETS", False);

    if (text)
    {
        if (req->target == targetsAtom)
        {
            Atom supported[] = {targetsAtom, utf8StringAtom, XA_STRING};
            XChangeProperty(display, req->requestor, property, XA_ATOM, 32, PropModeReplace,
                            (unsigned char *)supported, 3);
            reply.property = property;
        }
        else if (req->target == utf8StringAtom || req->target == XA_STRING)
        {
            XChangeProperty(display, req->requestor, property, req->target, 8, PropModeReplace,
                            (const unsigned char *)text->data(), (int)text->size());
            reply.property = property;
        }
    }

    XSendEvent(display, req->requestor, False, 0, (XEvent *)&reply);
    XFlush(display);
}

/**
 * @brief Make pasted text fit the single-line input.
 *
 * Trailing line breaks are dropped, inner line breaks and tabs become spaces,
 * and other control characters (which would show as garbage) are removed.
 */
static string Clean_Paste_Text(const string &raw)
{
    string text = raw;
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
        text.pop_back();

    string out;
    for (size_t i = 0; i < text.size(); i++)
    {
        unsigned char c = text[i];
        if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
            continue;
        if (c == '\n' || c == '\r' || c == '\t')
            out += ' ';
        else if (c >= 0x20 && c != 0x7f)
            out += (char)c;
    }
    return out;
}

/**
 * @brief Insert pasted selection data into the active tab's input line.
 *
 * Called on a SelectionNotify event. Reads the converted selection from
 * @c propertyAtom on the main window and, if it is 8-bit UTF8_STRING or STRING
 * data, inserts it (see Clean_Paste_Text()) at the cursor position and scrolls
 * to the prompt. The property is deleted afterwards.
 *
 * @param event The SelectionNotify event returned by the X server.
 */
void ProcessPasteData(XSelectionEvent *event)
{
    if (event->property == None || Tabs.empty())
        return;

    Atom actualType;
    int actualFormat;
    unsigned char *data = nullptr;
    unsigned long nItems, bytesAfter;

    int readResult = XGetWindowProperty(
        display,
        window,
        propertyAtom,
        0,
        LONG_MAX,
        False,
        AnyPropertyType,
        &actualType,
        &actualFormat,
        &nItems,
        &bytesAfter,
        &data);

    if (readResult == Success && data && nItems > 0)
    {
        if ((actualType == utf8StringAtom || actualType == XA_STRING) && actualFormat == 8)
        {
            Tab &T = *Tabs[Active_Tab];
            string pasteData = Clean_Paste_Text(string((char *)data, nItems));

            T.writing_index = min(T.writing_index, (int)T.line.size());
            T.line.insert(T.writing_index, pasteData);
            T.writing_index += pasteData.size();

            Scroll_To_Bottom(T);
            Draw_UI();
        }
    }

    if (data)
        XFree(data);

    XDeleteProperty(display, window, propertyAtom);
}
