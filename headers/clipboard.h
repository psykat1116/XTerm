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

void ProcessPasteData(XSelectionEvent *event)
{
    if (event->property == None)
    {
        cout << "Error" << endl;
        return;
    }

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
            string pasteData = string((char *)data, nItems);

            T.line.insert(T.writing_index, pasteData);
            T.writing_index += pasteData.size();

            Draw_UI();
        }
    }

    if (data)
        XFree(data);

    XDeleteProperty(display, window, propertyAtom);
}
