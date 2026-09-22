/*
 * Regression tests for ExecCmdSync().
 *
 * The original implementation called waitpid() before reading the output
 * pipe. When the child produced more than the pipe buffer (~64KiB), the
 * child blocked on write while the parent blocked in waitpid(): a classic
 * two-way deadlock that froze dumpsys/top-app/brightness paths in dfps.
 *
 * These tests run both on a POSIX host (via test.sh) and on Android devices.
 */

#include "utils/misc.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        ++g_checks;                                                                                                    \
        if (!(cond)) {                                                                                                 \
            ++g_failures;                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                       \
        }                                                                                                              \
    } while (0)

static const char *FindExisting(const char *const *paths) {
    struct stat st;
    for (const char *const *p = paths; *p != nullptr; ++p) {
        if (stat(*p, &st) == 0) {
            return *p;
        }
    }
    return nullptr;
}

static const char *FindSh() {
    static const char *paths[] = {"/system/bin/sh", "/bin/sh", nullptr};
    return FindExisting(paths);
}

static const char *FindEcho() {
    static const char *paths[] = {"/system/bin/echo", "/bin/echo", nullptr};
    return FindExisting(paths);
}

// Fixed 256-byte output line, repeated $1 times.
static constexpr char WRITE_LINES_SCRIPT[] =
    "i=0; while [ $i -lt $1 ]; do printf '%s\\n' \"$2\"; i=$((i+1)); done";

static constexpr char LINE_255[] =
    "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
    "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
    "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
    "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF01234567";

static constexpr size_t LINE_CHARS = sizeof(LINE_255) - 1;   // 255
static constexpr size_t LINE_BYTES = LINE_CHARS + 1;         // incl. '\n'

static void TestSmallOutput() {
    const char *echo = FindEcho();
    if (echo == nullptr) {
        std::fprintf(stderr, "SKIP TestSmallOutput: no echo binary\n");
        return;
    }
    std::string out;
    int rc = ExecCmdSync(&out, echo, "hello", "world", nullptr);
    CHECK(rc == 0);
    CHECK(std::string(out.c_str()) == "hello world\n");
}

static void TestEmptyOutput() {
    const char *sh = FindSh();
    std::string out = "junk";
    int rc = ExecCmdSync(&out, sh, "-c", "true", nullptr);
    CHECK(rc == 0);
    CHECK(out.empty());
}

// Core regression: output far larger than the 64KiB pipe buffer must return
// promptly. With the buggy waitpid-first implementation this hangs forever.
static void TestLargeOutputNoDeadlock() {
    const char *sh = FindSh();
    constexpr int ITERATIONS = 4097; // 1MiB + one line, not pipe-batch aligned
    const size_t expectedBytes = static_cast<size_t>(ITERATIONS) * LINE_BYTES;

    std::string out;
    int64_t start = GetNowTs();
    int rc = ExecCmdSync(&out, sh, "-c", WRITE_LINES_SCRIPT, "_",
                         std::to_string(ITERATIONS).c_str(), LINE_255, nullptr);
    int64_t elapsedMs = UsToMs(GetNowTs() - start);

    CHECK(rc == 0);
    CHECK(elapsedMs < 10000);
    CHECK(std::strlen(out.c_str()) == expectedBytes);
    bool wellFormed = (out.size() == expectedBytes + 1 && out.back() == '\0');
    for (size_t i = 0; wellFormed && i + 1 < out.size(); ++i) {
        char c = out[i];
        if (c != '\n' && !(c >= '0' && c <= '9') && !(c >= 'A' && c <= 'F')) {
            wellFormed = false;
        }
        if (c == '\n' && i % LINE_BYTES != LINE_BYTES - 1) {
            wellFormed = false;
        }
    }
    CHECK(wellFormed);
}

// Both stdout and stderr are dup2'ed onto the same pipe; heavy interleaved
// writes must not deadlock either.
static void TestMergedStderrNoDeadlock() {
    const char *sh = FindSh();
    const char *script =
        "i=0; while [ $i -lt 4097 ]; do "
        "printf '%s\\n' \"$1\"; printf '%s\\n' \"$1\" >&2; i=$((i+1)); done";

    std::string out;
    int64_t start = GetNowTs();
    int rc = ExecCmdSync(&out, sh, "-c", script, "_", LINE_255, nullptr);
    int64_t elapsedMs = UsToMs(GetNowTs() - start);

    CHECK(rc == 0);
    CHECK(elapsedMs < 10000);
    CHECK(std::strlen(out.c_str()) > 64 * 1024);
}

// content == nullptr: no pipe is created at all, a heavy child must still be
// reaped cleanly.
static void TestNullContentLargeChild() {
    const char *sh = FindSh();
    int64_t start = GetNowTs();
    int rc = ExecCmdSync(nullptr, sh, "-c", WRITE_LINES_SCRIPT, "_", "4097", LINE_255, nullptr);
    int64_t elapsedMs = UsToMs(GetNowTs() - start);

    CHECK(rc == 0);
    CHECK(elapsedMs < 10000);
}

static void TestExitCodePropagation() {
    const char *sh = FindSh();
    std::string out;
    CHECK(ExecCmdSync(&out, sh, "-c", "exit 7", nullptr) == 7);
    CHECK(out.empty());
}

static void TestExecFailure() {
    std::string out;
    int rc = ExecCmdSync(&out, "/no/such/binary/dfps_test", nullptr);
    CHECK(rc == 255); // child calls exit(-1) after execv() failure
    CHECK(out.empty());
}

int main() {
    TestSmallOutput();
    TestEmptyOutput();
    TestLargeOutputNoDeadlock();
    TestMergedStderrNoDeadlock();
    TestNullContentLargeChild();
    TestExitCodePropagation();
    TestExecFailure();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
