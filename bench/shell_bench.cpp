// End-to-end backend benchmark: drives the real ./function shell backend.
// Also checks the front end <-> backend protocol (echo output, multi-line
// quote markers, Ctrl+C cancel) before timing anything.
//
// Build:  g++ -std=c++17 -O2 bench/shell_bench.cpp -o bench/shell_bench
// Run:    ./bench/shell_bench [must be run from the project root, next to the built ./function binary]

#include <chrono>
#include <cstdio>
#include <string>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

using namespace std;
using Clock = chrono::steady_clock;

/**
 * @brief A running ./function backend and the pipes connected to it.
 */
typedef struct
{
    pid_t pid = -1;   ///< PID of the backend process.
    int fd_read = -1; ///< Non-blocking read end of the backend's stdout/stderr.
    int fd_write = -1; ///< Write end of the backend's stdin.
} Backend;

/**
 * @brief Start ./function connected by pipes, the same way Create_Tab() does.
 *
 * Exits the benchmark if pipe() or fork() fails.
 *
 * @return The started backend.
 */
static Backend Spawn_Function()
{
    Backend b;
    int p2c[2], c2p[2];
    if (pipe(p2c) < 0 || pipe(c2p) < 0)
    {
        perror("pipe");
        exit(1);
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork");
        exit(1);
    }
    if (pid == 0)
    {
        dup2(p2c[0], STDIN_FILENO);
        dup2(c2p[1], STDOUT_FILENO);
        dup2(c2p[1], STDERR_FILENO);
        close(p2c[0]);
        close(p2c[1]);
        close(c2p[0]);
        close(c2p[1]);
        setsid();
        execvp("./function", (char *const[]){(char *)"./function", nullptr});
        _exit(1);
    }

    close(p2c[0]);
    close(c2p[1]);
    int flags = fcntl(c2p[0], F_GETFL, 0);
    fcntl(c2p[0], F_SETFL, flags | O_NONBLOCK);

    b.pid = pid;
    b.fd_read = c2p[0];
    b.fd_write = p2c[1];
    return b;
}

/**
 * @brief Send a command to the backend and read its output until it finishes.
 *
 * Completion is detected by the 0x02 byte the backend writes when a
 * foreground command exits.
 *
 * @param b         Backend to use.
 * @param line      Command line to send (without newline).
 * @param timeout_s Give up after this many seconds without completion.
 * @param capture   If not null, receives all output bytes (including earlier
 *                  output from built-ins, which send no marker of their own).
 * @return Number of output bytes read before the completion marker.
 */
static size_t Run_Command_And_Wait(Backend &b, const string &line, double timeout_s = 30.0,
                                   string *capture = nullptr)
{
    string cmd = line + "\n";
    if (write(b.fd_write, cmd.data(), cmd.size()) < 0)
        perror("write");

    size_t total_bytes = 0;
    char buf[65536];
    auto start = Clock::now();
    while (true)
    {
        ssize_t n = read(b.fd_read, buf, sizeof(buf));
        if (n > 0)
        {
            if (capture)
                capture->append(buf, n);
            for (ssize_t k = 0; k < n; k++)
                if (buf[k] == '\x02')
                    return total_bytes;
            total_bytes += n;
        }
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            if (chrono::duration<double>(Clock::now() - start).count() > timeout_s)
                return total_bytes;
            usleep(500);
        }
        else
        {
            return total_bytes; // pipe closed
        }
    }
}

/**
 * @brief Measure how fast the backend streams a large file through `cat`.
 *
 * Generates a base64 payload in /tmp, times `cat` on it, prints the result and
 * deletes the file.
 *
 * @param b          Backend to use.
 * @param payload_mb Size of the random data in MiB (the base64 file is about 4/3 larger).
 */
static void Bench_Bulk_Throughput(Backend &b, size_t payload_mb)
{
    string path = "/tmp/xterm_bench_payload.txt";
    string gen = "head -c " + to_string(payload_mb * 1024 * 1024) +
                 " /dev/urandom | base64 > " + path;
    if (system(gen.c_str()) != 0)
        fprintf(stderr, "warning: payload generation command failed\n");

    auto start = Clock::now();
    size_t bytes = Run_Command_And_Wait(b, "cat " + path);
    auto end = Clock::now();

    double seconds = chrono::duration<double>(end - start).count();
    double mb = (double)bytes / (1024.0 * 1024.0);
    printf("Bulk throughput (cat %zu MB) : %9.5f s  ->  %8.2f MB/s (%zu bytes read back)\n",
           payload_mb, seconds, mb / seconds, bytes);

    remove(path.c_str());
}

/**
 * @brief Measure per-command overhead by running `true` repeatedly.
 *
 * @param b Backend to use.
 * @param n Number of commands to run.
 */
static void Bench_Command_Latency(Backend &b, int n)
{
    auto start = Clock::now();
    for (int i = 0; i < n; i++)
        Run_Command_And_Wait(b, "true");
    auto end = Clock::now();

    double seconds = chrono::duration<double>(end - start).count();
    printf("Command dispatch latency    : %d commands in %.3f s  ->  %.2f cmds/s (%.3f ms/cmd)\n",
           n, seconds, n / seconds, 1000.0 * seconds / n);
}

/** @brief Send one raw line to the backend without waiting for anything. */
static void Send_Line(Backend &b, const string &line)
{
    string cmd = line + "\n";
    if (write(b.fd_write, cmd.data(), cmd.size()) < 0)
        perror("write");
}

/** @brief Print one PASS/FAIL line and return whether it passed. */
static bool Check(const char *name, bool ok)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    return ok;
}

/**
 * @brief Check the front end <-> backend protocol the UI depends on.
 *
 * Built-ins send no completion marker, so each check ends with `true`, whose
 * marker tells us all earlier output has arrived.
 *
 * - Built-in echo still prints after an external command has written to stdout
 *   (it used to be lost when echo printed with wcout).
 * - An unclosed quote sends CONTINUATION_START (0x0e), the next line closes it,
 *   CONTINUATION_END (0x0f) follows, and both lines are printed.
 * - A cancel line (0x03) ends the continuation without printing anything.
 *
 * @return Number of failed checks.
 */
static int Check_Protocol(Backend &b)
{
    int failed = 0;
    string out;

    Run_Command_And_Wait(b, "true"); // makes stdout byte-oriented, like any external command
    Send_Line(b, "echo bench-echo-ok");
    Run_Command_And_Wait(b, "true", 5.0, &out);
    failed += !Check("built-in echo prints after an external command", out.find("bench-echo-ok") != string::npos);

    out.clear();
    Send_Line(b, "echo \"line one");
    Send_Line(b, "line two\"");
    Run_Command_And_Wait(b, "true", 5.0, &out);
    failed += !Check("unclosed quote sends continuation start/end markers",
                     out.find('\x0e') != string::npos && out.find('\x0f') != string::npos);
    failed += !Check("continued quote prints both lines", out.find("line one\nline two") != string::npos);

    out.clear();
    Send_Line(b, "echo \"bench-cancelled");
    Send_Line(b, "\x03"); // what the front end sends on Ctrl+C
    Run_Command_And_Wait(b, "true", 5.0, &out);
    failed += !Check("Ctrl+C cancel ends the continuation", out.find('\x0f') != string::npos);
    failed += !Check("cancelled echo prints nothing", out.find("bench-cancelled") == string::npos);

    return failed;
}

/**
 * @brief Run the protocol checks, then the command-latency and bulk-throughput
 *        benchmarks against ./function.
 *
 * @return 0 on success, 1 if ./function is missing or a protocol check failed.
 */
int main()
{
    if (access("./function", X_OK) != 0)
    {
        fprintf(stderr, "error: ./function not found or not executable. Run `make` in the project root first.\n");
        return 1;
    }

    printf("XTerm shell-backend benchmark (drives ./function exactly like Create_Tab())\n");
    printf("-----------------------------------------------------------------------------\n");

    printf("Protocol checks:\n");
    Backend b0 = Spawn_Function();
    int failed = Check_Protocol(b0);
    kill(b0.pid, SIGTERM);
    waitpid(b0.pid, nullptr, 0);
    printf("\n");

    Backend b1 = Spawn_Function();
    Bench_Command_Latency(b1, 200);
    kill(b1.pid, SIGTERM);
    waitpid(b1.pid, nullptr, 0);

    Backend b2 = Spawn_Function();
    Bench_Bulk_Throughput(b2, 64);
    Bench_Bulk_Throughput(b2, 256);
    Bench_Bulk_Throughput(b2, 512);
    kill(b2.pid, SIGTERM);
    waitpid(b2.pid, nullptr, 0);

    return failed ? 1 : 0;
}
