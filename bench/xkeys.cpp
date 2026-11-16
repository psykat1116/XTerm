// Tiny helper for render_bench.sh: types text into a window by sending
// synthetic X11 key events (no XTest/xdotool needed).
//
// Build:  g++ -std=c++17 -O2 bench/xkeys.cpp -o bench/xkeys -lX11
// Usage:  bench/xkeys <window-id> type "seq 1 20000"
//         bench/xkeys <window-id> key Return

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>

/** @brief Send one KeyPress for @p ks to @p w, adding Shift if the key needs it. */
static void Send_Key(Display *d, Window w, KeySym ks)
{
    KeyCode kc = XKeysymToKeycode(d, ks);
    if (kc == 0)
    {
        fprintf(stderr, "xkeys: no key for keysym 0x%lx\n", ks);
        return;
    }

    XKeyEvent e = {};
    e.type = KeyPress;
    e.display = d;
    e.window = w;
    e.root = DefaultRootWindow(d);
    e.time = CurrentTime;
    e.same_screen = True;
    e.keycode = kc;
    e.state = XkbKeycodeToKeysym(d, kc, 0, 0) != ks ? ShiftMask : 0;
    
    XSendEvent(d, w, True, KeyPressMask, (XEvent *)&e);
    XFlush(d);
    usleep(10000);
}

int main(int argc, char **argv)
{
    if (argc != 4 || (strcmp(argv[2], "type") != 0 && strcmp(argv[2], "key") != 0))
    {
        fprintf(stderr, "usage: %s <window-id> type <text> | key <KeysymName>\n", argv[0]);
        return 2;
    }

    Display *d = XOpenDisplay(nullptr);
    if (!d)
    {
        fprintf(stderr, "xkeys: cannot open display\n");
        return 1;
    }
    Window w = strtoul(argv[1], nullptr, 0);

    if (strcmp(argv[2], "type") == 0)
    {
        for (const char *p = argv[3]; *p; p++)
        {
            char name[2] = {*p, 0};
            KeySym ks = *p == ' ' ? XK_space : *p == '-' ? XK_minus
                                           : *p == '.'   ? XK_period
                                                         : XStringToKeysym(name);
            Send_Key(d, w, ks);
        }
    }
    else
        Send_Key(d, w, XStringToKeysym(argv[3]));

    XCloseDisplay(d);
    return 0;
}
