#include <pty.h>
#include <pwd.h>
#include <poll.h>
#include <wait.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <semaphore.h>

#include <sys/mman.h>
#include <sys/stat.h>

#include "headers/tab.h"
#include "headers/util.h"
#include "headers/history.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/Xlocale.h>
#include <X11/Xft/Xft.h>

#include <string>
#include <chrono>
#include <vector>
#include <locale>
#include <memory>
#include <cctype>
#include <climits>
#include <fstream>
#include <cstring>
#include <sstream>
#include <iostream>
#include <algorithm>

using namespace std;

GC gc;
int screen;
Window window;
Display *display;

Atom primaryAtom;
Atom propertyAtom;
Atom clipboardAtom;
Atom utf8StringAtom;

XftColor Xft_Color;
XftFont *Xft_Font = nullptr;
XftDraw *Xft_Draw = nullptr;

sem_t *Shared_History_Semaphore = nullptr;
SharedHistory *Shared_History_Data = nullptr;

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

void Draw_UI();
void Process();
void Handle_Signal(int);
void ProcessPasteData(XSelectionEvent *);
void Add_To_Buffer(Tab &, const string &);
static inline int UTF8_Width(const string &);
void DrawUtf8(XftDraw *, XftFont *, XftColor *, int, int, const string &);

int main(int argc, char *argv[])
{
    signal(SIGINT, Handle_Signal);
    signal(SIGQUIT, Handle_Signal);
    signal(SIGTSTP, Handle_Signal);

    Init_Shared_Memory();
    Load_History_From_File();

    if (setlocale(LC_ALL, "") == NULL)
        cerr << "Set Locale Error" << endl;
    if (!XSupportsLocale() || XSetLocaleModifiers("") == NULL)
        cerr << "Local Support Error " << endl;

    display = XOpenDisplay(nullptr);
    if (display == nullptr)
    {
        cerr << "Error Opening Display" << endl;
        return 1;
    }

    primaryAtom = XInternAtom(display, "PRIMARY", False);
    clipboardAtom = XInternAtom(display, "CLIPBOARD", False);
    utf8StringAtom = XInternAtom(display, "UTF8_STRING", False);
    propertyAtom = XInternAtom(display, "XTERM_CLIPBOARD_PROP", False);

    screen = DefaultScreen(display);
    window = XCreateSimpleWindow(
        display,
        RootWindow(display, screen),
        0, 0,
        Win_Width, Win_Height,
        10,
        WhitePixel(display, screen),
        BlackPixel(display, screen));

    XMapWindow(display, window);
    XSelectInput(display, window, ExposureMask | KeyPressMask | StructureNotifyMask | ButtonPressMask | SelectionNotify);
    XStoreName(display, window, "XTerm [Ctrl-T new tab, Ctrl-W close tab, Esc close]");

    gc = XCreateGC(display, window, 0, nullptr);

    Visual *Visual = DefaultVisual(display, screen);
    Colormap Colormap = DefaultColormap(display, screen);

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

    Xft_Font = nullptr;
    for (auto &fname : font_name_list)
    {
        Xft_Font = XftFontOpenName(display, screen, fname);
        if (Xft_Font)
            break;
    }
    if (!Xft_Font)
    {
        cerr << "Fatal Error: No Xft font available" << endl;
        return 1;
    }

    Xft_Draw = XftDrawCreate(display, window, Visual, Colormap);
    if (!Xft_Draw)
    {
        cerr << "Fatal Error: XftDrawCreate failed" << endl;
        XftFontClose(display, Xft_Font);
        return 1;
    }

    XRenderColor renderColor = {0xffff, 0xffff, 0xffff, 0xffff};
    if (!XftColorAllocValue(display, Visual, Colormap, &renderColor, &Xft_Color))
    {
        cerr << "Warning: XftColorAllocValue failed" << endl;
    }

    unique_ptr<Tab> First_Tab = Create_Tab();
    if (First_Tab)
    {
        Tabs.emplace_back(move(First_Tab));
        Active_Tab = 0;
    }
    else
    {
        cerr << "Failed To Create Tab" << endl;
    }

    Draw_UI();
    Process();

    Cleanup_Shared_Memory();

    if (Xft_Draw)
    {
        XftDrawDestroy(Xft_Draw);
        Xft_Draw = nullptr;
    }
    if (Xft_Font)
    {
        XftFontClose(display, Xft_Font);
        Xft_Font = nullptr;
    }
    XftColorFree(display, Visual, Colormap, &Xft_Color);

    XFreeGC(display, gc);
    XDestroyWindow(display, window);
    XCloseDisplay(display);

    return 0;
}

void Process()
{
    static const int Blink_Interval = 500;
    static const int Edit_Interval = 5000;
    static auto Last_Blink = chrono::steady_clock::now();
    static auto Edit_Blink = chrono::steady_clock::now();

    XEvent event;
    for (bool running = true; running;)
    {
        if (Tabs.empty())
        {
            usleep(100000);
            while (XPending(display))
            {
                XNextEvent(display, &event);
                if (event.type == Expose)
                    Draw_UI();
                else if (event.type == DestroyNotify)
                    running = false;
            }
            continue;
        }

        vector<struct pollfd> File_Descriptors;
        for (auto &T : Tabs)
            if (T->is_alive && T->fd_read >= 0)
                File_Descriptors.push_back({T->fd_read, POLLIN, 0});

        int Timeout_MS = 120, RC = 0;
        if (!File_Descriptors.empty())
            RC = poll(File_Descriptors.data(), File_Descriptors.size(), Timeout_MS);
        else
            RC = poll(nullptr, 0, Timeout_MS);

        if (RC < 0 && errno != EINTR)
            perror("All Tabs Polling Error");

        size_t Index = 0;
        for (size_t i = 0; i < Tabs.size(); i++)
        {
            unique_ptr<Tab> &T = Tabs[i];
            if (!(T->is_alive && T->fd_read >= 0))
                continue;

            bool got_eof_or_error = false;
            if (Index < File_Descriptors.size() && File_Descriptors[Index].revents & POLLIN)
            {
                char Buffer[4096];
                ssize_t Read_Byte_Count = read(T->fd_read, Buffer, sizeof(Buffer));
                if (Read_Byte_Count > 0)
                {
                    Add_To_Buffer(*T, string(Buffer, Buffer + Read_Byte_Count));
                }
                else if (Read_Byte_Count == 0)
                {
                    T->is_alive = false;
                    close(T->fd_read);
                    T->fd_read = -1;
                }
                else if (errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    T->is_alive = false;
                    if (T->fd_read >= 0)
                    {
                        close(T->fd_read);
                        T->fd_read = -1;
                    }
                }
            }

            if (T->is_alive && T->fd_read >= 0)
                Index++;
        }

        while (XPending(display))
        {
            XNextEvent(display, &event);
            switch (event.type)
            {
            case Expose:
            {
                Draw_UI();
                break;
            }
            case KeyPress:
            {
                KeySym keysym;
                char Key_Buffer[32];
                Edit_Blink = chrono::steady_clock::now();
                bool IS_CTRL = event.xkey.state & ControlMask;
                int len = XLookupString(&event.xkey, Key_Buffer, sizeof(Key_Buffer), &keysym, nullptr);

                if (keysym == XK_Escape)
                {
                    running = false;
                }
                else if (IS_CTRL && (keysym == XK_t || Key_Buffer[0] == 't'))
                {
                    unique_ptr<Tab> New_Tab = Create_Tab();
                    if (New_Tab)
                    {
                        Tabs.emplace_back(move(New_Tab));
                        Active_Tab = Tabs.size() - 1;
                        Draw_UI();
                    }
                }
                else if (IS_CTRL && (keysym == XK_w || Key_Buffer[0] == 'w'))
                {
                    if (Tabs.size() > 1)
                    {
                        Close_Tab(Active_Tab);
                        Draw_UI();
                        break;
                    }
                }
                else if (IS_CTRL && (keysym == XK_c || Key_Buffer[0] == 'c'))
                {
                    Tab &T = *Tabs[Active_Tab];
                    if (T.child_pid >= 0)
                        kill(T.child_pid, SIGINT);

                    Add_To_Buffer(T, Get_Current_Path(T.child_pid) + T.line + "^C\n");
                    T.line.clear();
                    Draw_UI();
                }
                else if (IS_CTRL && (keysym == XK_z || Key_Buffer[0] == 'z'))
                {
                    Tab &T = *Tabs[Active_Tab];
                    Add_To_Buffer(T, Get_Current_Path(T.child_pid) + T.line + "^Z\n");
                    if (T.child_pid > 0)
                    {
                        kill(T.child_pid, SIGTSTP);
                        T.line.clear();
                        T.writing_index = 0;
                        Draw_UI();
                    }
                }
                else if (IS_CTRL && (keysym == XK_d || Key_Buffer[0] == 'd'))
                {
                    Tab &T = *Tabs[Active_Tab];
                    if (T.fd_write >= 0)
                    {
                        char EOF_Char = 4;
                        if (write(T.fd_write, &EOF_Char, 1) < 0)
                            perror("Error Writing EOF Character");
                    }
                    Draw_UI();
                }
                else if (IS_CTRL && (keysym == XK_a || Key_Buffer[0] == 'a'))
                {
                    Tab &T = *Tabs[Active_Tab];
                    T.writing_index = 0;
                    Draw_UI();
                }
                else if (IS_CTRL && (keysym == XK_e || Key_Buffer[0] == 'e'))
                {
                    Tab &T = *Tabs[Active_Tab];
                    T.writing_index = T.line.size();
                    Draw_UI();
                }
                else if (IS_CTRL && (keysym == XK_r || Key_Buffer[0] == 'r'))
                {
                    Tab &T = *Tabs[Active_Tab];
                    T.is_search = !T.is_search;
                    T.writing_index = T.line.size();
                    Draw_UI();
                }
                else if (IS_CTRL && (keysym == XK_v || Key_Buffer[0] == 'v'))
                {
                    XConvertSelection(
                        display,
                        clipboardAtom,
                        utf8StringAtom,
                        propertyAtom,
                        window,
                        event.xkey.time);

                    XFlush(display);
                }
                else if (IS_CTRL && keysym == XK_Left)
                {
                    if (!Tabs.empty())
                    {
                        Tabs[Active_Tab]->is_search = false;
                        Active_Tab = (Active_Tab - 1 + Tabs.size()) % Tabs.size();
                        Draw_UI();
                        break;
                    }
                }
                else if (IS_CTRL && keysym == XK_Right)
                {
                    if (!Tabs.empty())
                    {
                        Tabs[Active_Tab]->is_search = false;
                        Active_Tab = (Active_Tab + 1) % Tabs.size();
                        Draw_UI();
                        break;
                    }
                }
                else if (keysym >= XK_0 && keysym <= XK_9)
                {
                    Tab &T = *Tabs[Active_Tab];
                    if (T.is_autocomplete)
                    {
                        Add_To_Buffer(T, T.line);

                        int Options = keysym - XK_0;
                        if (Options >= 0 && Options < T.suggestions.size())
                            T.line = T.temp + T.suggestions[Options];
                        else
                            T.line = "";

                        T.writing_index = T.line.size();
                        T.is_autocomplete = false;
                        T.temp = "";
                        Draw_UI();
                    }
                    else
                    {
                        T.line.insert(T.writing_index, Key_Buffer, len);
                        T.writing_index += len;
                        Draw_UI();
                    }
                }
                else if (keysym == XK_Tab)
                {
                    Tab &T = *Tabs[Active_Tab];
                    if (T.line.empty())
                        return;

                    vector<string> args = Split_Text(T.line);
                    string Target = args[args.size() - 1], Suggestion;

                    T.suggestions = Get_Suggestion_From_Directory(T.child_pid, Target);

                    if (T.suggestions.size() == 1)
                    {
                        for (int i = 0; i < args.size() - 1; i++)
                            Suggestion += args[i] + " ";
                        Suggestion += T.suggestions[0];

                        T.line = Suggestion;
                        T.suggestions.clear();
                        T.writing_index = T.line.size();
                        Draw_UI();
                    }
                    else if (T.suggestions.size() > 1)
                    {
                        Add_To_Buffer(T, Get_Current_Path(T.child_pid) + T.line + "\n");
                        for (int i = 0; i < T.suggestions.size(); i++)
                        {
                            Suggestion += to_string(i) + " - " + T.suggestions[i];
                            if (i < T.suggestions.size() - 1)
                                Suggestion += " ";
                        }
                        for (int i = 0; i < args.size() - 1; i++)
                        {
                            T.temp += args[i];
                            if (i < args.size() - 1)
                                T.temp += " ";
                        }

                        T.is_autocomplete = true;
                        T.line = Suggestion;
                        Draw_UI();
                    }
                }
                else if (keysym == XK_Return)
                {
                    Tab &T = *Tabs[Active_Tab];
                    if (!T.line.empty())
                        Update_Shared_History(T.line);

                    if (T.is_autocomplete)
                    {
                        T.line = T.temp;
                        T.is_autocomplete = false;
                    }
                    else if (T.is_search)
                    {
                        vector<string> Match_Commands = Load_Search_From_File(T.line);
                        for (auto mc : Match_Commands)
                            Add_To_Buffer(T, mc + "\n");

                        T.line.clear();
                        T.writing_index = 0;
                        T.is_search = false;
                    }
                    else
                    {
                        if (Trim(T.line) == "clear")
                        {
                            T.buffer.clear();
                            if (!T.line.empty())
                                Update_Shared_History(T.line);
                        }
                        else if (Trim(T.line) == "exit")
                        {
                            running = false;
                            break;
                        }
                        else
                        {
                            string tosend = T.line + "\n";
                            if (T.fd_write >= 0)
                                write(T.fd_write, tosend.c_str(), tosend.size());
                            Add_To_Buffer(T, Get_Current_Path(T.child_pid) + T.line + "\n");
                        }
                    }

                    T.history_index = -1;
                    T.writing_index = 0;
                    T.line.clear();
                    Draw_UI();
                }
                else if (keysym == XK_BackSpace)
                {
                    Tab &T = *Tabs[Active_Tab];
                    if (!T.line.empty())
                    {
                        if (T.writing_index > 0)
                        {
                            T.line.erase(T.writing_index - 1, 1);
                            T.writing_index--;
                        }
                        Draw_UI();
                    }
                }
                else if (keysym == XK_Page_Up)
                {
                    Tab &T = *Tabs[Active_Tab];
                    T.scroll_index -= 5;
                    if (T.scroll_index < 0)
                        T.scroll_index = 0;
                    Draw_UI();
                }
                else if (keysym == XK_Page_Down)
                {
                    Tab &T = *Tabs[Active_Tab];
                    int Total_Lines = T.buffer.size();
                    int font_ascent = Xft_Font ? Xft_Font->ascent : 12;
                    int font_descent = Xft_Font ? Xft_Font->descent : 6;
                    int Max_Lines_Visible = (Win_Height - Tab_Height - font_ascent - font_descent) / (font_ascent + font_descent);
                    T.scroll_index += 3;
                    if (T.scroll_index > Total_Lines - Max_Lines_Visible)
                        T.scroll_index = max(0, Total_Lines - Max_Lines_Visible);
                    Draw_UI();
                }
                else if (keysym == XK_Left)
                {
                    Tab &T = *Tabs[Active_Tab];
                    if (!T.is_autocomplete)
                        if (--T.writing_index < 0)
                            T.writing_index = 0;
                    Draw_UI();
                }
                else if (keysym == XK_Right)
                {
                    Tab &T = *Tabs[Active_Tab];
                    if (!T.is_autocomplete)
                        T.writing_index = min(++T.writing_index, (int)T.line.size());
                    Draw_UI();
                }
                else if (keysym == XK_Up)
                {
                    Tab &T = *Tabs[Active_Tab];
                    T.is_search = false;
                    if (T.is_autocomplete)
                    {
                        T.is_autocomplete = false;
                        T.line = T.temp;
                        T.temp = "";
                    }
                    Load_Tab_History(T, 1);
                    T.writing_index = T.line.size();
                    Draw_UI();
                }
                else if (keysym == XK_Down)
                {
                    Tab &T = *Tabs[Active_Tab];
                    T.is_search = false;
                    if (T.is_autocomplete)
                    {
                        T.is_autocomplete = false;
                        T.line = T.temp;
                        T.temp = "";
                    }

                    if (T.history_index == 0)
                    {
                        T.line.clear();
                        T.writing_index = 0;
                        T.history_index = -1;
                    }
                    else if (T.history_index > 0)
                        Load_Tab_History(T, -1);

                    Draw_UI();
                }
                else if (len > 0)
                {
                    Tab &T = *Tabs[Active_Tab];
                    T.line.insert(T.writing_index, Key_Buffer, len);
                    T.writing_index += len;
                    Draw_UI();
                }
                break;
            }
            case ButtonPress:
            {
                int X = 0;
                bool handled = false;
                Edit_Blink = chrono::steady_clock::now();
                XButtonEvent *be = (XButtonEvent *)&event;
                for (size_t i = 0; i < Tabs.size(); i++)
                {
                    int Tab_W = TAB_WIDTH;
                    int Tab_Left = X;
                    int Tab_Right = X + Tab_W;
                    int Close_X = Tab_Right - CLOSE_BOX_W - 6;
                    int Close_Y = (Tab_Height - CLOSE_BOX_H) / 2;

                    if (be->y >= Close_Y && be->y <= Close_Y + CLOSE_BOX_H && be->x >= Close_X && be->x <= Close_X + CLOSE_BOX_W)
                    {
                        Close_Tab(i);
                        Draw_UI();
                        handled = true;
                        break;
                    }
                    if (be->y <= Tab_Height && be->x >= Tab_Left && be->x <= Tab_Right)
                    {
                        Active_Tab = i;
                        Draw_UI();
                        handled = true;
                        break;
                    }

                    X += Tab_W;
                }
                if (handled)
                    continue;

                int Plus_X = X + 8;
                int Plus_Y = (Tab_Height - PLUS_BOX_H) / 2;
                if (be->y >= Plus_Y && be->y <= Plus_Y + PLUS_BOX_H && be->x >= Plus_X && be->x <= Plus_X + PLUS_BOX_W)
                {
                    auto New_Tab = Create_Tab();
                    if (New_Tab)
                    {
                        Tabs.emplace_back(move(New_Tab));
                        Active_Tab = Tabs.size() - 1;
                        Draw_UI();
                    }
                    continue;
                }
                if (be->button == Button4)
                {
                    Tab &T = *Tabs[Active_Tab];
                    T.scroll_index -= 3;
                    if (T.scroll_index < 0)
                        T.scroll_index = 0;
                    Draw_UI();
                }
                else if (be->button == Button5)
                {
                    Tab &T = *Tabs[Active_Tab];
                    int font_ascent = Xft_Font ? Xft_Font->ascent : 12;
                    int font_descent = Xft_Font ? Xft_Font->descent : 6;
                    int Max_Lines_Visible = (Win_Height - Tab_Height - font_ascent - font_descent) / (font_ascent + font_descent);
                    int Total_Lines = T.buffer.size();
                    T.scroll_index += 3;
                    if (T.scroll_index > Total_Lines - Max_Lines_Visible)
                        T.scroll_index = max(0, Total_Lines - Max_Lines_Visible);
                    Draw_UI();
                }

                break;
            }
            case SelectionNotify:
            {
                Edit_Blink = chrono::steady_clock::now();

                if (event.xselection.selection == clipboardAtom)
                {
                    if (event.xselection.property == None)
                    {
                        XConvertSelection(
                            display,
                            primaryAtom,
                            utf8StringAtom,
                            propertyAtom,
                            window,
                            event.xselection.time);

                        XFlush(display);
                    }
                    else
                        ProcessPasteData(&event.xselection);
                }
                else if (event.xselection.selection == primaryAtom)
                    ProcessPasteData(&event.xselection);

                break;
            }
            case ConfigureNotify:
            {
                Edit_Blink = chrono::steady_clock::now();
                XConfigureEvent *CE = (XConfigureEvent *)&event;

                Win_Width = CE->width;
                Win_Height = CE->height;

                Draw_UI();
                break;
            }
            case DestroyNotify:
                running = false;
                break;
            }
        }

        auto Current_Time = chrono::steady_clock::now();
        auto Elapsed_Blink_Time = chrono::duration_cast<chrono::milliseconds>(Current_Time - Last_Blink).count();
        auto Elapsed_Edit_Time = chrono::duration_cast<chrono::milliseconds>(Current_Time - Edit_Blink).count();

        if (Elapsed_Edit_Time > Edit_Interval)
        {
            Cursor_Visible = True;
            Draw_UI();
        }
        else if (Elapsed_Blink_Time > Blink_Interval)
        {
            Cursor_Visible = !Cursor_Visible;
            Last_Blink = Current_Time;
            Draw_UI();
        }

        XFlush(display);
        usleep(50000);
    }
}

void Draw_UI()
{
    XClearWindow(display, window);
    XSetForeground(display, gc, 0xFF2F2A29);
    XFillRectangle(display, window, gc, 0, 0, Win_Width, Tab_Height);

    int Tab_X = 0;
    for (size_t i = 0; i < Tabs.size(); i++)
    {
        bool active = (int)i == Active_Tab;
        int Tab_W = TAB_WIDTH;

        string Tab_Title = Tabs[i]->title;
        if (active)
        {
            XSetForeground(display, gc, BlackPixel(display, screen));
            XFillRectangle(display, window, gc, Tab_X + 1, 1, Tab_W - 1, Tab_Height - 2);
        }
        else
        {
            XSetForeground(display, gc, 0xFF2F2A29);
            XFillRectangle(display, window, gc, Tab_X + 1, 1, Tab_W - 1, Tab_Height - 2);
        }

        XSetForeground(display, gc, WhitePixel(display, screen));
        int Close_X = Tab_X + Tab_W - CLOSE_BOX_W - 6;
        int Close_Y = (Tab_Height - CLOSE_BOX_H) / 2;

        DrawUtf8(Xft_Draw, Xft_Font, &Xft_Color, Close_X + 3, Close_Y + CLOSE_BOX_H - 3, "x");
        DrawUtf8(Xft_Draw, Xft_Font, &Xft_Color, Tab_X + TAB_PADDING_L, Tab_Height - 8, Tab_Title);

        Tab_X += Tab_W;
    }

    int Plus_X = Tab_X + 8;
    int Plus_Y = (Tab_Height - PLUS_BOX_H) / 2;
    XDrawRectangle(display, window, gc, Plus_X, Plus_Y, PLUS_BOX_W, PLUS_BOX_H);
    DrawUtf8(Xft_Draw, Xft_Font, &Xft_Color, Plus_X + 5, Plus_Y + PLUS_BOX_H - 2, "+");

    XDrawLine(display, window, gc, 0, Tab_Height, Win_Width, Tab_Height);

    if (Tabs.empty())
    {
        DrawUtf8(Xft_Draw, Xft_Font, &Xft_Color, 8, Tab_Height + 20, "NO TABS");
        XFlush(display);
        return;
    }

    Tab &T = *Tabs[Active_Tab];
    int font_ascent = Xft_Font ? Xft_Font->ascent : 12;
    int font_descent = Xft_Font ? Xft_Font->descent : 6;
    int line_height = font_ascent + font_descent;

    int Y = Tab_Height + line_height;
    int Total_Lines = T.buffer.size();
    int Max_Lines_Visible = (Win_Height - Tab_Height - font_ascent - font_descent) / line_height;

    if (T.scroll_index > Total_Lines - Max_Lines_Visible)
        T.scroll_index = max(0, Total_Lines - Max_Lines_Visible);

    for (int i = T.scroll_index; i < Total_Lines && Y < Win_Height; i++)
    {
        string Draw_Line = T.buffer[i];
        if (Draw_Line.size() > 1000)
            Draw_Line = Draw_Line.substr(0, 1000);
        DrawUtf8(Xft_Draw, Xft_Font, &Xft_Color, 5, Y, Draw_Line);
        Y += line_height;
    }

    string Prefix;
    if (T.is_search)
        Prefix = "एक दिन पहले मेरा दोसत अलासका गया था: ";
    else if (T.is_autocomplete)
        Prefix = "> ";
    else
        Prefix = Get_Current_Path(T.child_pid);

    string in = Prefix + T.line;
    DrawUtf8(Xft_Draw, Xft_Font, &Xft_Color, 5, Y, in);

    string in_for_cursor = Prefix + T.line.substr(0, min((size_t)T.writing_index, T.line.size()));
    int Cursor_X, Cursor_Y = Y - font_ascent + 2;
    if (Xft_Font)
    {
        XGlyphInfo ext;
        XftTextExtentsUtf8(display, Xft_Font, (FcChar8 *)in_for_cursor.c_str(), (int)in_for_cursor.size(), &ext);
        Cursor_X = 5 + ext.xOff;
    }
    else
    {
        Cursor_X = 5 + (in_for_cursor.size() * 8);
    }

    if (Cursor_Visible && !T.is_autocomplete)
    {
        int cursor_h = line_height - 4;
        if (cursor_h < 1)
            cursor_h = line_height;
        XFillRectangle(display, window, gc, Cursor_X, Cursor_Y, 2, cursor_h);
    }

    XFlush(display);
}

void Handle_Signal(int signal)
{
    for (auto &T : Tabs)
    {
        if (T->is_alive && T->child_pid > 0)
        {
            kill(T->child_pid, SIGTERM);
            if (T->fd_read >= 0)
                close(T->fd_read);
            if (T->fd_write >= 0)
                close(T->fd_write);
        }
    }

    if (signal != 0)
    {
        Cleanup_Shared_Memory();
        _exit(0);
    }
}

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

void Add_To_Buffer(Tab &t, const string &data)
{
    istringstream iss(data);
    string line;

    while (getline(iss, line))
    {
        t.buffer.push_back(line);
        if (t.buffer.size() > 4000)
        {
            t.buffer.erase(t.buffer.begin(), t.buffer.begin() + 1000);
        }
    }

    int font_ascent = Xft_Font ? Xft_Font->ascent : 12;
    int font_descent = Xft_Font ? Xft_Font->descent : 6;
    int Max_Lines_Visible = (Win_Height - Tab_Height - font_ascent - font_descent) / (font_ascent + font_descent);
    t.scroll_index = max(0, (int)t.buffer.size() - Max_Lines_Visible);

    Draw_UI();
}

void DrawUtf8(XftDraw *draw, XftFont *font, XftColor *color, int x, int y, const string &s)
{
    if (!draw || !font)
        return;
    XftDrawStringUtf8(draw, color, font, x, y, (const FcChar8 *)s.c_str(), (int)s.size());
}