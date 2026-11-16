#pragma once

#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <iostream>
#include <algorithm>

#include <limits.h>
#include <unistd.h>

using namespace std;

/**
 * @brief Foreground colors supported by the ANSI/SGR renderer.
 *
 * Values 1-8 are the standard colors (SGR 30-37), 9-16 the bright colors
 * (SGR 90-97). @c Count is the number of entries and sizes the palette.
 */
typedef enum
{
    Default = 0,
    Black,
    Red,
    Green,
    Yellow,
    Blue,
    Magenta,
    Cyan,
    White,
    BrightBlack,
    BrightRed,
    BrightGreen,
    BrightYellow,
    BrightBlue,
    BrightMagenta,
    BrightCyan,
    BrightWhite,
    PromptHost,   ///< Theme prompt color for "user@host" (private SGR 200).
    PromptParent, ///< Theme prompt color for the parent part of the path (private SGR 201).
    PromptDir,    ///< Theme prompt color for the current folder (private SGR 202).
    PromptArrow,  ///< Theme prompt color for the "❯" (private SGR 203).
    Count
} AnsiColor;

/**
 * @brief A run of text drawn in a single color.
 */
typedef struct
{
    string text;                          ///< UTF-8 text without escape sequences.
    AnsiColor color = AnsiColor::Default; ///< Effective color to draw the text with.
} Segment;

/**
 * @brief State of one terminal tab and its shell backend process.
 */
struct Tab
{
    pid_t child_pid; ///< PID of the forked ./function shell backend.

    bool is_alive = false;        ///< True while the backend is running and its pipes are open.
    bool is_search = false;       ///< True while in reverse history search mode (Ctrl-R).
    bool is_autocomplete = false; ///< True while a numbered Tab-completion menu is shown.
    bool foreground_busy = false; ///< True while the backend runs an interactive foreground command.
    bool continuation = false;    ///< True while the backend waits for more lines (e.g. an unclosed quote).

    int fd_read = -1;       ///< Non-blocking read end of the backend's stdout/stderr pipe.
    int fd_write = -1;      ///< Write end of the backend's stdin pipe.
    int scroll_index = 0;   ///< Index of the first scrollback line shown.
    int writing_index = 0;  ///< Cursor position (byte offset) within #line.
    int history_index = -1; ///< Position while browsing history; -1 when not browsing.

    string line = "";  ///< Current contents of the input line.
    string temp = "";  ///< Command text before the completed word, saved during autocomplete.
    string title = ""; ///< Label shown in the tab bar.

    string raw_pending = "";                      ///< Incomplete escape or UTF-8 bytes carried over to the next read.
    bool current_bold = false;                    ///< Bold state from the last SGR sequence.
    vector<Segment> current_line;                 ///< Output line being built, not yet ended by a newline.
    AnsiColor current_color = AnsiColor::Default; ///< Foreground color from the last SGR sequence.

    vector<vector<Segment>> buffer; ///< Scrollback of completed output lines.
    vector<string> suggestions;     ///< Candidates offered by the latest Tab completion.
};

int Active_Tab;
vector<unique_ptr<Tab>> Tabs;

void Close_Tab(int);
unique_ptr<Tab> Create_Tab();

/**
 * @brief Close a tab, terminating its backend and releasing its pipes.
 *
 * Sends SIGHUP to the backend, closes both pipe ends and removes the tab from
 * #Tabs, clamping #Active_Tab to a valid index. Does nothing if @p index is
 * out of range or only one tab is open.
 *
 * @param index Index of the tab in #Tabs.
 */
void Close_Tab(int index)
{
    if (index < 0 || index >= (int)Tabs.size() || Tabs.size() == 1)
        return;

    auto &T = Tabs[index];
    if (T->is_alive)
    {
        kill(T->child_pid, SIGHUP);
        if (T->fd_read >= 0)
        {
            close(T->fd_read);
            T->fd_read = -1;
        }
        if (T->fd_write >= 0)
        {
            close(T->fd_write);
            T->fd_write = -1;
        }
        T->is_alive = false;
    }

    Tabs.erase(Tabs.begin() + index);
    if (Tabs.empty())
        Active_Tab = 0;
    else
        Active_Tab = min(Active_Tab, (int)Tabs.size() - 1);
}

/**
 * @brief Create a new tab backed by a fresh ./function shell process.
 *
 * Creates two pipes, forks, and execs ./function in a new session with its
 * stdin, stdout and stderr connected to the pipes. The parent's read end is
 * made non-blocking.
 *
 * @return The new tab, or nullptr if pipe() or fork() failed.
 */
unique_ptr<Tab> Create_Tab()
{
    int Parent_To_Child[2];
    int Child_To_Parent[2];
    if (pipe(Parent_To_Child) < 0)
    {
        perror("Pipe Parent_To_Children");
        return nullptr;
    }
    if (pipe(Child_To_Parent) < 0)
    {
        perror("pipe Child_To_Parent");
        close(Parent_To_Child[0]);
        close(Parent_To_Child[1]);
        return nullptr;
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        perror("Fork Error");
        close(Parent_To_Child[0]);
        close(Parent_To_Child[1]);
        close(Child_To_Parent[0]);
        close(Child_To_Parent[1]);
        return nullptr;
    }
    if (pid == 0)
    {
        dup2(Parent_To_Child[0], STDIN_FILENO);
        dup2(Child_To_Parent[1], STDOUT_FILENO);
        dup2(Child_To_Parent[1], STDERR_FILENO);

        close(Parent_To_Child[0]);
        close(Parent_To_Child[1]);
        close(Child_To_Parent[0]);
        close(Child_To_Parent[1]);

        setsid();

        char *const argv[] = {(char *)"./function", nullptr};
        execvp("./function", argv);

        _exit(1);
    }
    else
    {
        close(Parent_To_Child[0]);
        close(Child_To_Parent[1]);

        int flags = fcntl(Child_To_Parent[0], F_GETFL, 0);
        if (flags >= 0)
            fcntl(Child_To_Parent[0], F_SETFL, flags | O_NONBLOCK);

        unique_ptr t = make_unique<Tab>();
        t->child_pid = pid;
        t->fd_read = Child_To_Parent[0];
        t->fd_write = Parent_To_Child[1];
        t->is_alive = true;
        t->title = "Tab - " + to_string(pid);

        return t;
    }
}