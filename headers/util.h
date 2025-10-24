#pragma once

#include <glob.h>
#include <limits.h>

#include <ctime>
#include <vector>
#include <string>
#include <cstring>
#include <sstream>
#include <fstream>
#include <iostream>
#include <filesystem>

#define HISTORY_FILE_NAME ".terminal.history.txt"

using namespace std;
namespace fs = std::filesystem;

string Current_Time();
string Trim(const string &);
string Get_Current_Path(pid_t);
vector<string> Split_Text(string &);
string UTF8_To_Wstring(const wstring &);
string Wstring_To_UTF8(const wstring &);
vector<string> Command_Parser(const string &);
vector<string> Load_Search_From_File(string &);
int Count_Unescapes_Double_Quotes(const string &);
string Expand_Environmental_Varibables(const string &);
static bool Find_LCS_String(const string &, const string &);
vector<string> Get_Suggestion_From_Directory(pid_t, string &);
bool Extract_First_Last_Unescaped_Quotes(const string &, string &);

string Current_Time()
{
    time_t now = time(nullptr);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return string(buf);
}

string Trim(const string &str)
{
    size_t a = 0;
    while (a < str.size() && isspace((unsigned char)str[a]))
        ++a;
    ssize_t b = (ssize_t)str.size() - 1;
    while (b >= 0 && isspace((unsigned char)str[b]))
        --b;
    if (b < (ssize_t)a)
        return string();
    return str.substr(a, b - a + 1);
}

string Get_Current_Path(pid_t pid)
{
    const char *user = getenv("USER");
    if (!user)
    {
        struct passwd *pw = getpwuid(getuid());
        user = pw ? pw->pw_name : "user";
    }

    char hostname[256];
    gethostname(hostname, sizeof(hostname));

    char buffer[PATH_MAX];
    string Proc_Path = "/proc/" + to_string(pid) + "/cwd";
    ssize_t len = readlink(Proc_Path.c_str(), buffer, sizeof(buffer) - 1);

    string prompt = string(user) + "@" + hostname;

    if (len != -1)
    {
        buffer[len] = '\0';
        prompt = prompt + ":" + buffer + "$ ";
    }
    else
        prompt += "$ ";

    return prompt;
}

vector<string> Split_Text(string &cmd)
{
    vector<string> args;
    istringstream iss(cmd);

    string arg;
    while (iss >> arg)
    {
        string Part_String = Trim(arg);
        if (!Part_String.empty())
            args.push_back(Part_String);
    }

    return args;
}

wstring UTF8_To_Wstring(const string &str)
{
    wstring out;
    mbstate_t st{};
    const char *ptr = str.c_str();

    size_t left = str.size();
    while (left > 0)
    {
        wchar_t wc;
        size_t ret = mbrtowc(&wc, ptr, left, &st);
        if (ret == (size_t)-1 || ret == (size_t)-2)
        {
            ptr++;
            left--;
            memset(&st, 0, sizeof(st));
            continue;
        }
        if (ret == 0)
        {
            ptr++;
            left--;
            out.push_back(L'\0');
            memset(&st, 0, sizeof(st));
            continue;
        }
        out.push_back(wc);
        ptr += ret;
        left -= ret;
    }

    return out;
}

string Wstring_To_UTF8(const wstring &wstr)
{
    string out;
    mbstate_t st{};
    char buf[MB_CUR_MAX];
    for (wchar_t wc : wstr)
    {
        size_t len = wcrtomb(buf, wc, &st);
        if (len == (size_t)-1)
        {
            memset(&st, 0, sizeof(st));
            continue;
        }
        out.append(buf, len);
    }
    return out;
}

vector<string> Command_Parser(const string &cmd)
{
    vector<string> tokens;
    string token;
    bool in_quotes = false;
    char quote_char = 0;

    for (size_t i = 0; i < cmd.size(); ++i)
    {
        char c = cmd[i];

        if ((c == '"' || c == '\'') && !in_quotes)
        {
            in_quotes = true;
            quote_char = c;
        }
        else if (in_quotes && c == quote_char)
        {
            in_quotes = false;
        }
        else if (!in_quotes && isspace((unsigned char)c))
        {
            if (!token.empty())
            {
                tokens.push_back(token);
                token.clear();
            }
        }
        else
        {
            token.push_back(c);
        }
    }

    if (!token.empty())
        tokens.push_back(token);

    vector<string> expanded;
    for (auto &arg : tokens)
    {
        glob_t results;
        memset(&results, 0, sizeof(results));

        if (arg.find('*') != string::npos ||
            arg.find('?') != string::npos ||
            arg.find('[') != string::npos)
        {
            int ret = glob(arg.c_str(), 0, nullptr, &results);
            if (ret == 0)
            {
                for (size_t i = 0; i < results.gl_pathc; i++)
                    expanded.push_back(results.gl_pathv[i]);
            }
            else
            {
                expanded.push_back(arg);
            }
            globfree(&results);
        }
        else
        {
            expanded.push_back(arg);
        }
    }

    return expanded;
}

vector<string> Load_Search_From_File(string &search)
{
    if (search.empty())
        return {};

    string line;
    vector<string> Match_Commands;
    fstream History_File(HISTORY_FILE_NAME, ios::in | ios::app);
    if (!History_File.is_open())
    {
        cerr << "ERROR OPENING HISTORY FILE!!" << HISTORY_FILE_NAME << endl;
        return {"No history matches for: " + search};
    }

    while (getline(History_File, line))
        if (!line.empty() && line == search)
            Match_Commands.push_back(line);

    if (Match_Commands.empty())
    {
        History_File.clear();
        History_File.seekg(0);
        while (getline(History_File, line))
            if (!line.empty() && Find_LCS_String(search, line))
                Match_Commands.push_back(line);
    }

    if (Match_Commands.empty())
        Match_Commands.push_back("No history matches for: " + search);
    else
        Match_Commands.insert(Match_Commands.begin(), "History matches for: " + search);

    History_File.close();

    return Match_Commands;
}

int Count_Unescaped_Double_Quotes(const string &str)
{
    int count = 0;
    for (size_t i = 0; i < str.size(); i++)
    {
        if (str[i] == '"')
        {
            bool escaped = false;
            if (i > 0 && str[i - 1] == '\\')
            {
                size_t j = i, backlashes = 0;
                while (j > 0 && str[--j] == '\\')
                    backlashes++;
                if (backlashes % 2 == 1)
                    escaped = true;
            }
            if (!escaped)
                ++count;
        }
    }
    return count;
}

string Expand_Environmental_Variables(const string &input)
{
    string result;

    for (size_t i = 0; i < input.size(); i++)
    {
        if (input[i] == '$')
        {
            string var_name = "";

            i++;
            while (i < input.size() && input[i] != ' ')
            {
                var_name += input[i];
                i++;
            }

            if (!var_name.empty())
            {
                const char *val = getenv(var_name.c_str());
                if (val)
                    result += val;
                else
                    result += "$" + var_name;
            }

            if (input[i] == ' ')
                result += ' ';
        }
        else
        {
            result += input[i];
        }
    }

    return result;
}

static bool Find_LCS_String(const string &s1, const string &s2)
{
    int m = s1.length();
    int n = s2.length();
    if (m == 0 || n == 0)
        return 0;

    int max_len = 0;
    vector<vector<int>> dp(m + 1, vector<int>(n + 1, 0));

    for (int i = 1; i <= m; ++i)
    {
        for (int j = 1; j <= n; ++j)
        {
            if (s1[i - 1] == s2[j - 1])
            {
                dp[i][j] = dp[i - 1][j - 1] + 1;
                if (dp[i][j] > max_len)
                {
                    max_len = dp[i][j];
                    if (max_len > 2)
                        return true;
                }
            }
            else
                dp[i][j] = 0;
        }
    }

    return max_len > 2;
}

vector<string> Get_Suggestion_From_Directory(pid_t pid, string &Prefix)
{
    vector<string> Files_In_Directory;
    string Process_Path = "/proc/" + to_string(pid) + "/cwd";

    fs::path Directory_Path(Process_Path);
    if (!fs::exists(Directory_Path) || !fs::is_directory(Directory_Path))
    {
        cerr << "Error: path does not exist or is not a directory" << endl;
        return Files_In_Directory;
    }

    try
    {
        for (auto &File : fs::directory_iterator(Directory_Path))
        {
            const fs::path &File_Path = File.path();
            if (File.is_regular_file() || File.is_directory())
            {
                string File_Name = File_Path.filename().string();
                if (File.is_directory())
                    File_Name += "/";
                if (File_Name.rfind(Prefix, 0) == 0)
                    Files_In_Directory.push_back(File_Name);
            }
        }
    }
    catch (const fs::filesystem_error &File_Error)
    {
        cerr << "Filesystem Error While Reading Directory" << File_Error.what() << endl;
    }

    return Files_In_Directory;
}

bool Extract_First_Last_Unescaped_Quotes(const string &s, string &out_content)
{
    ssize_t first = -1, last = -1;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '"')
        {
            bool escaped = false;
            size_t j = i;
            size_t backslashes = 0;
            while (j > 0 && s[--j] == '\\')
                backslashes++;
            if (backslashes % 2 == 1)
                escaped = true;
            if (!escaped)
            {
                first = (ssize_t)i;
                break;
            }
        }
    }
    if (first < 0)
        return false;
    for (ssize_t i = (ssize_t)s.size() - 1; i > first; --i)
    {
        if (s[i] == '"')
        {
            bool escaped = false;
            ssize_t j = i;
            size_t backslashes = 0;
            while (j > 0 && s[--j] == '\\')
                backslashes++;
            if (backslashes % 2 == 1)
                escaped = true;
            if (!escaped)
            {
                last = i;
                break;
            }
        }
    }
    if (last <= first)
        return false;
    out_content = s.substr(first + 1, last - first - 1);
    string tmp;
    tmp.reserve(out_content.size());
    for (size_t i = 0; i < out_content.size(); ++i)
    {
        if (out_content[i] == '\\' && i + 1 < out_content.size())
        {
            char nx = out_content[i + 1];
            if (nx == '"' || nx == '\\')
            {
                tmp.push_back(nx);
                i++;
                continue;
            }
            tmp.push_back('\\');
        }
        else
            tmp.push_back(out_content[i]);
    }
    out_content.swap(tmp);
    return true;
}