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

struct Tab
{
    pid_t child_pid;

    bool is_alive = false;
    bool is_search = false;
    bool is_autocomplete = false;

    int fd_read = -1;
    int fd_write = -1;
    int scroll_index = 0;
    int writing_index = 0;
    int history_index = -1;

    string line = "";
    string temp = "";
    string title = "";

    vector<string> buffer;
    vector<string> suggestions;
};

int Active_Tab;
vector<unique_ptr<Tab>> Tabs;

void Close_Tab(int);
unique_ptr<Tab> Create_Tab();

void Close_Tab(int index)
{
    if (index < 0 || index >= Tabs.size() || Tabs.size() == 1)
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