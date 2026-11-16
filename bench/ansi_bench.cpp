// Microbenchmark for the production ANSI/SGR escape-sequence parser
// (headers/ansi_core.h::AnsiToSegments). Pure CPU, no X11/PTY involved.
//
// Build:  g++ -std=c++17 -O2 -I.. bench/ansi_bench.cpp -o bench/ansi_bench
// Run:    ./bench/ansi_bench

#include <chrono>
#include <cstdio>
#include <random>
#include <string>

// headers/tab.h assumes these are already pulled in by whatever includes
// it (as terminal.cpp/function.cpp do); provide them here for standalone use.
#include <csignal>
#include <fcntl.h>
#include <unistd.h>

#include "../headers/ansi_core.h"

using namespace std;
using Clock = chrono::steady_clock;

/**
 * @brief Build a synthetic payload that looks like colorized CLI output.
 *
 * Mimics output such as `ls --color`, `grep --color` or build tool logs:
 * short runs of plain text interleaved with SGR color/reset escape codes.
 * A fixed random seed makes the payload the same on every run.
 *
 * @param target_bytes Minimum payload size in bytes.
 * @return The generated payload.
 */
static string Make_Payload(size_t target_bytes)
{
    // Standard SGR colors plus the private 200-203 codes the prompt uses for
    // the theme's prompt colors (see ApplySGR()).
    static const char *codes[] = {
        "\x1b[31m", "\x1b[32m", "\x1b[33m", "\x1b[34m", "\x1b[35m",
        "\x1b[36m", "\x1b[1;31m", "\x1b[1;32m", "\x1b[0m", "\x1b[39m",
        "\x1b[200m", "\x1b[202m"};
    static const char *words[] = {
        "drwxr-xr-x", "src/", "main.cpp", "10", "README.md",
        "warning:", "error:", "PASS", "FAIL", "127.0.0.1"};

    mt19937 rng(42);
    uniform_int_distribution<int> code_pick(0, 11);
    uniform_int_distribution<int> word_pick(0, 9);

    string out;
    out.reserve(target_bytes + 64);
    while (out.size() < target_bytes)
    {
        out += codes[code_pick(rng)];
        out += words[word_pick(rng)];
        out += ' ';
        if (word_pick(rng) == 0)
            out += '\n';
    }
    
    return out;
}

/**
 * @brief Time AnsiToSegments() on one payload size and print the throughput.
 *
 * @param label         Name of the case shown in the output.
 * @param payload_bytes Payload size passed to Make_Payload().
 * @param iterations    Number of timed parser calls.
 */
static void Run_Case(const char *label, size_t payload_bytes, int iterations)
{
    string payload = Make_Payload(payload_bytes);

    // Warm-up (page faults, allocator warm cache, branch predictor).
    volatile size_t sink = 0;
    for (int i = 0; i < 3; i++)
        sink += AnsiToSegments(payload).size();

    auto start = Clock::now();
    for (int i = 0; i < iterations; i++)
        sink += AnsiToSegments(payload).size();
    auto end = Clock::now();

    double seconds = chrono::duration<double>(end - start).count();
    double total_mb = (double)payload.size() * iterations / (1024.0 * 1024.0);
    double mb_per_s = total_mb / seconds;
    double calls_per_s = iterations / seconds;

    printf("%-22s payload=%6zu B  iters=%6d  time=%7.3f s  throughput=%8.2f MB/s  %10.0f calls/s\n",
           label, payload.size(), iterations, seconds, mb_per_s, calls_per_s);
}

/**
 * @brief Time parsing of the prompt line, which Draw_UI() does on every redraw.
 *
 * Uses the same layout Get_Current_Path() produces:
 * "user@host ~/parent/dir ❯ " with the private prompt color codes.
 *
 * @param iterations Number of timed parser calls.
 */
static void Run_Prompt_Case(int iterations)
{
    const string prompt = "\x1b[200muser@workstation\x1b[0m \x1b[201m~/Projects/\x1b[0m"
                          "\x1b[202mXTerm\x1b[0m \x1b[203m\xe2\x9d\xaf\x1b[0m ";

    volatile size_t sink = 0;
    for (int i = 0; i < 1000; i++)
        sink += AnsiToSegments(prompt).size();

    auto start = Clock::now();
    for (int i = 0; i < iterations; i++)
        sink += AnsiToSegments(prompt).size();
    auto end = Clock::now();

    double seconds = chrono::duration<double>(end - start).count();
    printf("%-22s payload=%6zu B  iters=%6d  time=%7.3f s  %8.0f ns/call  %10.0f calls/s\n",
           "prompt (per redraw)", prompt.size(), iterations, seconds,
           1e9 * seconds / iterations, iterations / seconds);
}

/**
 * @brief Run the parser benchmark for 1 KB, 64 KB, 1 MB and 16 MB payloads,
 *        plus the prompt line parsed on every redraw.
 *
 * @return 0.
 */
int main()
{
    printf("XTerm ANSI/SGR parser microbenchmark (headers/ansi_core.h::AnsiToSegments)\n");
    printf("----------------------------------------------------------------------------\n");

    Run_Case("small (1 KB)", 1024, 20000);
    Run_Case("medium (64 KB)", 64 * 1024, 2000);
    Run_Case("large (1 MB)", 1024 * 1024, 200);
    Run_Case("huge (16 MB)", 16 * 1024 * 1024, 15);
    Run_Prompt_Case(200000);

    return 0;
}
