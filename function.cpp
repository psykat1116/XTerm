#include <pwd.h>
#include <glob.h>
#include <poll.h>
#include <wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <semaphore.h>

#include <ctime>
#include <locale>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <csignal>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <filesystem>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "headers/util.h"
#include "headers/history.h"

#define MAX_HISTORY_SIZE 1000

using namespace std;

typedef struct
{
    int fd;
    pid_t pid;
    string cmd;
    string tempfile;
} Watchprocess;

pid_t shell_pgid;
vector<pid_t> g_child_pid;
vector<Watchprocess> WProcess;

string g_pending_input;
pid_t g_foreground_pid = -1;
int g_interactive_write_fd = -1;
bool g_frontend_stdin_closed = false;

sem_t *Shared_History_Semaphore = nullptr;
SharedHistory *Shared_History_Data = nullptr;

void Handle_Signal(int);
void Run_Command(string &);

static void Block_Watch_Signals(sigset_t &old_mask)
{
    sigset_t block_mask;
    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIGINT);
    sigaddset(&block_mask, SIGTSTP);
    sigprocmask(SIG_BLOCK, &block_mask, &old_mask);
}

static void Restore_Watch_Signals(const sigset_t &old_mask)
{
    sigprocmask(SIG_SETMASK, &old_mask, nullptr);
}

static void Redirect_Stdin_To_DevNull()
{
    int DevNull_FD = open("/dev/null", O_RDONLY);
    if (DevNull_FD != -1)
    {
        dup2(DevNull_FD, STDIN_FILENO);
        close(DevNull_FD);
    }
}

static bool Read_Next_Line(string &line)
{
    while (true)
    {
        size_t pos = g_pending_input.find('\n');
        if (pos != string::npos)
        {
            line = g_pending_input.substr(0, pos);
            g_pending_input.erase(0, pos + 1);
            return true;
        }

        char buf[4096];
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
        if (n <= 0)
            return false;
        g_pending_input.append(buf, n);
    }
}

int main()
{
    setlocale(LC_ALL, "");
    signal(SIGINT, Handle_Signal);
    signal(SIGTSTP, Handle_Signal);
    signal(SIGPIPE, SIG_IGN);

    shell_pgid = getpgrp();
    Attach_Shared_Memory();

    while (true)
    {
        if (g_foreground_pid > 0)
        {
            string chunk;
            if (!g_pending_input.empty())
            {
                chunk = move(g_pending_input);
                g_pending_input.clear();
            }
            else if (!g_frontend_stdin_closed)
            {
                struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
                int poll_rc = poll(&pfd, 1, 100);
                if (poll_rc > 0 && (pfd.revents & POLLIN))
                {
                    char buf[4096];
                    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
                    if (n > 0)
                        chunk.assign(buf, n);
                    else
                        g_frontend_stdin_closed = true;
                }
            }
            else
            {
                struct timespec ts = {0, 100 * 1000 * 1000};
                nanosleep(&ts, nullptr);
            }

            if (!chunk.empty() && g_interactive_write_fd != -1)
            {
                for (char c : chunk)
                {
                    if (c == '\x04')
                    {
                        close(g_interactive_write_fd);
                        g_interactive_write_fd = -1;
                        break;
                    }
                    if (write(g_interactive_write_fd, &c, 1) < 0)
                        break;
                }
            }

            if (g_foreground_pid > 0)
            {
                int status;
                pid_t ret_pid = waitpid(g_foreground_pid, &status, WNOHANG | WUNTRACED);
                if (ret_pid == g_foreground_pid)
                {
                    if (g_interactive_write_fd != -1)
                    {
                        close(g_interactive_write_fd);
                        g_interactive_write_fd = -1;
                    }
                    if (!WIFSTOPPED(status))
                        g_child_pid.clear();
                    if (shell_pgid > 0)
                        tcsetpgrp(STDIN_FILENO, shell_pgid);

                    cout << '\x02' << flush;
                    g_foreground_pid = -1;
                }
            }

            continue;
        }

        string line;
        if (!Read_Next_Line(line))
            break;

        Run_Command(line);
    }

    Detach_Shared_Memory();
    return 0;
}

void Handle_Signal(int sig)
{
    if (sig == SIGTSTP)
    {
        for (pid_t pid : g_child_pid)
        {
            if (pid > 0)
                kill(-pid, SIGTSTP);
        }

        if (shell_pgid > 0)
            tcsetpgrp(STDIN_FILENO, shell_pgid);

        return;
    }

    for (pid_t pid : g_child_pid)
    {
        if (pid > 0)
            kill(-pid, SIGTERM);
    }
    g_child_pid.clear();

    for (auto &WP : WProcess)
    {
        if (WP.pid > 0)
            kill(WP.pid, SIGTERM);
        if (WP.fd >= 0)
            close(WP.fd);
        remove(WP.tempfile.c_str());
    }
    WProcess.clear();
}

void Run_Command(string &cmd)
{
    if (cmd.empty())
        return;

    cmd = Trim(cmd);
    if (cmd.rfind("cd", 0) == 0)
    {
        if (cmd.size() > 2 && cmd[2] != ' ')
            cmd.insert(2, " ");

        string path;
        if (cmd.size() <= 3)
            path = getenv("HOME");
        else
            path = cmd.substr(3);

        if (chdir(path.c_str()) != 0)
            perror("CHANGE DIRECTORY ERROR");
    }
    else if (cmd.rfind("echo", 0) == 0)
    {
        string content;
        if (Extract_First_Last_Unescaped_Quotes(cmd, content))
        {
            string unescaped_content;
            for (size_t i = 0; i < content.size(); i++)
            {
                if (content[i] == '\\' && i + 1 < content.size())
                {
                    char next = content[i + 1];
                    switch (next)
                    {
                    case 'n':
                        unescaped_content += '\n';
                        i++;
                        break;
                    case 't':
                        unescaped_content += '\t';
                        i++;
                        break;
                    case '\\':
                        unescaped_content += '\\';
                        i++;
                        break;
                    case '"':
                        unescaped_content += '"';
                        i++;
                        break;
                    case '\'':
                        unescaped_content += '\'';
                        i++;
                        break;
                    default:
                        unescaped_content += '\\';
                    }
                }
                else
                {
                    unescaped_content += content[i];
                }
            }

            string expanded_string = Expand_Environmental_Variables(unescaped_content);

            wstring w = UTF8_To_Wstring(expanded_string);
            wcout << w << "\n";
        }
        else
        {
            string rest = Trim(cmd.substr(4));
            char quote_char = 0;

            if (!rest.empty())
            {
                if (rest.front() == '"')
                    quote_char = '"';
                else if (rest.front() == '\'')
                    quote_char = '\'';
            }
            if (quote_char == 0)
            {
                string expanded_string = Expand_Environmental_Variables(rest);
                wstring w = UTF8_To_Wstring(expanded_string);
                wcout << w << "\n";
            }
            else
            {
                string content_buffer = rest.substr(1);
                string next_line;
                bool found_end = false;
                bool eof = false;

                while (!found_end)
                {
                    size_t pos = 0;
                    while ((pos = content_buffer.find(quote_char, pos)) != string::npos)
                    {
                        if (pos == 0 || content_buffer[pos - 1] != '\\')
                        {
                            content_buffer = content_buffer.substr(0, pos);
                            found_end = true;
                            break;
                        }
                        pos++;
                    }

                    if (found_end)
                        break;

                    fflush(stdout);
                    if (Read_Next_Line(next_line))
                    {
                        content_buffer += "\n" + next_line;
                    }
                    else
                    {
                        eof = true;
                        break;
                    }
                }

                if (eof && !found_end)
                {
                    cout << "shell: warning: unterminated " << quote_char << " string" << endl;
                }

                string unescaped_content;
                for (size_t i = 0; i < content_buffer.size(); ++i)
                {
                    if (content_buffer[i] == '\\' && i + 1 < content_buffer.size())
                    {
                        char next = content_buffer[i + 1];
                        if (next == quote_char || next == '\\')
                        {
                            unescaped_content += next;
                            i++;
                        }
                        else
                        {
                            unescaped_content += '\\';
                        }
                    }
                    else
                    {
                        unescaped_content += content_buffer[i];
                    }
                }

                string expanded_string = Expand_Environmental_Variables(unescaped_content);

                wstring w = UTF8_To_Wstring(expanded_string);
                wcout << w << '\n';
            }
        }
    }
    else if (cmd == "history")
    {
        if (!Shared_History_Data || Shared_History_Data == MAP_FAILED || Shared_History_Semaphore == SEM_FAILED)
        {
            cout << "History not available (Shared Memory error/not attached)." << endl;
            return;
        }

        sem_wait(Shared_History_Semaphore);

        int History_Size = Shared_History_Data->count;
        if (History_Size > 0)
        {
            for (int i = 0; i < History_Size; i++)
            {
                cout << i + 1 << " " << Shared_History_Data->commands[i] << endl;
            }
        }

        sem_post(Shared_History_Semaphore);
    }
    else if (cmd.rfind("multiWatch") == 0)
    {
        WProcess.clear();

        string Command = "";
        vector<string> Commands;

        bool is_started = false;
        for (size_t i = 0; i < cmd.size(); i++)
        {
            if (cmd[i] == '"')
            {
                if (is_started == false)
                {
                    is_started = true;
                    continue;
                }
                else
                {
                    Commands.push_back(Command);
                    Command = "";
                    is_started = false;
                }
            }
            else if (is_started)
            {
                Command += cmd[i];
            }
        }

        int Command_Count = Commands.size();
        for (int i = 0; i < Command_Count; i++)
        {
            vector<char *> argv;
            vector<string> args = Command_Parser(Commands[i]);

            for (auto &a : args)
                argv.push_back(const_cast<char *>(a.data()));
            argv.push_back(nullptr);

            pid_t pid = fork();
            if (pid == 0)
            {
                setpgid(0, 0);
                signal(SIGTSTP, SIG_DFL);

                Redirect_Stdin_To_DevNull();

                string File_Name = ".temp." + to_string(getpid()) + ".txt";
                int File_Descriptor = open(File_Name.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
                if (File_Descriptor == -1)
                {
                    perror("Open Temp File Error");
                    _exit(1);
                }

                dup2(File_Descriptor, STDOUT_FILENO);
                dup2(File_Descriptor, STDERR_FILENO);
                close(File_Descriptor);

                execvp(argv[0], argv.data());
                perror("EXECVP ERROR!!");
                _exit(1);
            }
            else if (pid > 0)
            {
                setpgid(pid, pid);

                sigset_t old_mask;
                Block_Watch_Signals(old_mask);
                g_child_pid.push_back(pid);
                Restore_Watch_Signals(old_mask);

                string File_Name = ".temp." + to_string(pid) + ".txt";
                for (int j = 0; j < 10 && !filesystem::exists(File_Name); ++j)
                    usleep(100000);

                int File_Descriptor = open(File_Name.c_str(), O_RDONLY | O_NONBLOCK);

                Block_Watch_Signals(old_mask);
                WProcess.push_back({File_Descriptor, pid, Commands[i], File_Name});
                Restore_Watch_Signals(old_mask);
            }
            else
            {
                perror("FORK FAILED!!");
            }
        }

        while (!WProcess.empty())
        {
            size_t No_Of_Watch_Process = WProcess.size();
            vector<struct pollfd> Poll_FDs(No_Of_Watch_Process);
            for (size_t i = 0; i < No_Of_Watch_Process; i++)
            {
                Poll_FDs[i].fd = WProcess[i].fd;
                Poll_FDs[i].events = POLLIN;
                Poll_FDs[i].revents = 0;
            }

            int ret = poll(Poll_FDs.data(), Poll_FDs.size(), 1000);
            if (ret < 0)
                break;
            if (ret > 0)
            {
                for (size_t i = 0; i < No_Of_Watch_Process; i++)
                {
                    if (Poll_FDs[i].revents & POLLIN)
                    {
                        char buf[1024];
                        int bytes = read(Poll_FDs[i].fd, buf, sizeof(buf) - 1);
                        if (bytes > 0)
                        {
                            buf[bytes] = '\0';
                            cout << "\"" << WProcess[i].cmd << "\", " << Current_Time() << " :\n";
                            cout << "-------------------------------------------------------\n";
                            cout << buf;
                            cout << "-------------------------------------------------------\n";
                            fflush(stdout);
                        }
                    }
                }
            }

            sigset_t old_mask;
            Block_Watch_Signals(old_mask);

            for (ssize_t i = WProcess.size() - 1; i >= 0; i--)
            {
                int status = 0;
                pid_t ret_pid = waitpid(WProcess[i].pid, &status, WNOHANG | WUNTRACED);
                if (ret_pid == 0)
                    continue;
                else if (ret_pid == -1)
                {
                    if (WProcess[i].fd >= 0)
                        close(WProcess[i].fd);
                    remove(WProcess[i].tempfile.c_str());

                    auto it = find(g_child_pid.begin(), g_child_pid.end(), WProcess[i].pid);
                    if (it != g_child_pid.end())
                        g_child_pid.erase(it);
                    WProcess.erase(WProcess.begin() + i);
                }
                else if (WIFSTOPPED(status))
                {
                    WProcess.erase(WProcess.begin() + i);

                    if (shell_pgid > 0)
                        tcsetpgrp(STDIN_FILENO, shell_pgid);
                }
                else
                {
                    if (WProcess[i].fd >= 0)
                        close(WProcess[i].fd);
                    remove(WProcess[i].tempfile.c_str());

                    auto it = find(g_child_pid.begin(), g_child_pid.end(), WProcess[i].pid);
                    if (it != g_child_pid.end())
                        g_child_pid.erase(it);

                    WProcess.erase(WProcess.begin() + i);
                }
            }

            Restore_Watch_Signals(old_mask);
        }

        {
            sigset_t old_mask;
            Block_Watch_Signals(old_mask);

            for (auto &proc : WProcess)
            {
                auto it = find(g_child_pid.begin(), g_child_pid.end(), proc.pid);
                if (it != g_child_pid.end())
                    g_child_pid.erase(it);

                kill(proc.pid, SIGKILL);
                close(proc.fd);
                remove(proc.tempfile.c_str());
            }
            WProcess.clear();

            Restore_Watch_Signals(old_mask);
        }
    }
    else if (cmd.find('|') != string::npos)
    {
        string Command;
        vector<string> Commands;
        istringstream iss(cmd);

        while (getline(iss, Command, '|'))
            Commands.push_back(Trim(Command));

        int No_Of_Commands = Commands.size();
        vector<int[2]> Pipes(No_Of_Commands - 1);
        vector<pid_t> Pipe_PIDs;
        pid_t pgid = 0;

        for (int i = 0; i < No_Of_Commands - 1; i++)
        {
            if (pipe(Pipes[i]) == -1)
            {
                cout << "PIPE FAILED!!" << endl;
                return;
            }
        }

        for (int i = 0; i < No_Of_Commands; i++)
        {
            vector<char *> argv;
            vector<string> args = Command_Parser(Commands[i]);

            for (auto &a : args)
                argv.push_back(const_cast<char *>(a.data()));
            argv.push_back(nullptr);

            pid_t pid = fork();
            if (pid == 0)
            {
                if (pgid == 0)
                    setpgid(0, 0);
                else
                    setpgid(0, pgid);

                signal(SIGTSTP, SIG_DFL);

                if (i > 0)
                {
                    dup2(Pipes[i - 1][0], STDIN_FILENO);
                }
                else
                {
                    Redirect_Stdin_To_DevNull();
                }
                if (i < No_Of_Commands - 1)
                {
                    dup2(Pipes[i][1], STDOUT_FILENO);
                    dup2(Pipes[i][1], STDERR_FILENO);
                }

                for (int j = 0; j < No_Of_Commands - 1; j++)
                {
                    close(Pipes[j][0]);
                    close(Pipes[j][1]);
                }

                execvp(argv[0], argv.data());
                if (errno == ENOENT)
                    cerr << "-> Command '" << cmd << "' not found" << "\n"
                         << flush;
                else
                    perror("EXECVP FAILED");

                _exit(1);
            }
            else if (pid > 0)
            {
                if (pgid == 0)
                    pgid = pid;
                setpgid(pid, pgid);
                Pipe_PIDs.push_back(pid);
            }
            else
            {
                perror("FORK FAILED IN PIPE EXECUTION PART!!");
            }
        }

        tcsetpgrp(STDIN_FILENO, pgid);

        g_child_pid = Pipe_PIDs;
        for (int i = 0; i < No_Of_Commands - 1; i++)
        {
            close(Pipes[i][0]);
            close(Pipes[i][1]);
        }
        for (int i = 0; i < No_Of_Commands; i++)
        {
            int status;
            pid_t ret_pid;
            do
            {
                ret_pid = waitpid(Pipe_PIDs[i], &status, WUNTRACED);
            } while (ret_pid == -1 && errno == EINTR);

            if (WIFSTOPPED(status))
                goto pipe_cleanup;
        }
        g_child_pid.clear();
    pipe_cleanup:
        tcsetpgrp(STDIN_FILENO, shell_pgid);
    }
    else
    {
        bool Append_Mode = false;
        string Input_File, Output_File;

        size_t Append_Pos = cmd.find(">>");
        size_t Output_Pos = cmd.find('>');
        if (Append_Pos != string::npos)
        {
            Output_File = Trim(cmd.substr(Append_Pos + 2));
            cmd = Trim(cmd.substr(0, Append_Pos));
            Append_Mode = true;
        }
        else if (Output_Pos != string::npos)
        {
            Output_File = Trim(cmd.substr(Output_Pos + 1));
            cmd = Trim(cmd.substr(0, Output_Pos));
        }

        size_t Input_Pos = cmd.find('<');
        if (Input_Pos != string::npos)
        {
            Input_File = Trim(cmd.substr(Input_Pos + 1));
            cmd = Trim(cmd.substr(0, Input_Pos));
        }

        vector<char *> argv;
        vector<string> args = Command_Parser(cmd);

        for (auto &a : args)
            argv.push_back(const_cast<char *>(a.c_str()));
        argv.push_back(nullptr);

        bool Use_Interactive_Pipe = Input_File.empty();
        int Interactive_Pipe[2] = {-1, -1};
        if (Use_Interactive_Pipe && pipe(Interactive_Pipe) == -1)
        {
            perror("INTERACTIVE PIPE FAILED");
            Use_Interactive_Pipe = false;
        }

        pid_t pid = fork();
        if (pid == 0)
        {
            setpgid(0, 0);
            tcsetpgrp(STDIN_FILENO, getpid());

            signal(SIGTSTP, SIG_DFL);

            int Read_FD, Write_FD;
            if (!Input_File.empty())
            {
                Read_FD = open(Input_File.c_str(), O_RDONLY);
                if (Read_FD == -1)
                {
                    cout << "CANNOT OPEN READ FILE!!" << Input_File << endl;
                    _exit(1);
                }
                if (dup2(Read_FD, STDIN_FILENO) == -1)
                {
                    cout << "Dup2 FAILED!!" << endl;
                    close(Read_FD);
                    _exit(1);
                }
                close(Read_FD);
            }
            else if (Use_Interactive_Pipe)
            {
                dup2(Interactive_Pipe[0], STDIN_FILENO);
                close(Interactive_Pipe[0]);
                close(Interactive_Pipe[1]);
            }
            else
            {
                Redirect_Stdin_To_DevNull();
            }
            if (!Output_File.empty())
            {
                Write_FD = open(Output_File.c_str(), O_CREAT | O_WRONLY | (Append_Mode ? O_APPEND : O_TRUNC), 0644);
                if (Write_FD == -1)
                {
                    cout << "CANNOT OPEN WRITE FILE!!" << Output_File << endl;
                    _exit(1);
                }
                if (dup2(Write_FD, STDOUT_FILENO) == -1)
                {
                    cout << "Dup2 FAILED!!" << endl;
                    close(Write_FD);
                    _exit(-1);
                }
                if (dup2(Write_FD, STDERR_FILENO) == -1)
                {
                    cout << "Dup2 FAILED!!" << endl;
                    close(Write_FD);
                    _exit(1);
                }
                close(Write_FD);
            }

            execvp(argv[0], argv.data());
            if (errno == ENOENT)
                cerr << "-> Command '" << cmd << "' not found" << "\n"
                     << flush;
            else
                perror("EXECVP ERROR IN ELSE PART!!");

            _exit(1);
        }
        else if (pid > 0)
        {
            setpgid(pid, pid);
            tcsetpgrp(STDIN_FILENO, pid);
            g_child_pid.push_back(pid);

            if (Use_Interactive_Pipe)
            {
                close(Interactive_Pipe[0]);
                g_interactive_write_fd = Interactive_Pipe[1];
                g_foreground_pid = pid;
                g_frontend_stdin_closed = false;
                cout << '\x01' << flush;
            }
            else
            {
                int status;
                pid_t ret_pid;
                do
                {
                    ret_pid = waitpid(pid, &status, WUNTRACED);
                } while (ret_pid == -1 && errno == EINTR);

                if (!WIFSTOPPED(status))
                    g_child_pid.clear();

                tcsetpgrp(STDIN_FILENO, shell_pgid);
            }
        }
        else
        {
            perror("FORK FAILED IN ELSE PART!!");
            if (Use_Interactive_Pipe)
            {
                close(Interactive_Pipe[0]);
                close(Interactive_Pipe[1]);
            }
        }
    }
}