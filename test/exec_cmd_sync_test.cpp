// Regression tests for ExecCmdSync() in source/utils/misc.cpp.
//
// Key case: a child process writing far more than the pipe buffer (64 KiB)
// used to deadlock ExecCmdSync, because the parent blocked in waitpid()
// before draining the pipe while the child blocked in write(). These tests
// re-exec the test binary itself as the child and guard every case with a
// watchdog so a regression fails fast instead of hanging forever.

#include "misc.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

namespace {

constexpr int WATCHDOG_SECONDS = 30;
int gFailures = 0;

void OnWatchdog(int) {
    const char msg[] = "FAIL: watchdog fired, ExecCmdSync deadlocked\n";
    write(STDERR_FILENO, msg, sizeof(msg) - 1);
    _exit(124);
}

void ArmWatchdog() { alarm(WATCHDOG_SECONDS); }
void DisarmWatchdog() { alarm(0); }

void Check(bool ok, const char *name) {
    printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) {
        gFailures++;
    }
}

// Child mode: write `count` bytes of a deterministic pattern to stdout, then
// exit with `exitCode`.
int ChildEmitMain(const char *countStr, const char *exitCodeStr) {
    long count = atol(countStr);
    int exitCode = atoi(exitCodeStr);

    char buf[4096];
    long emitted = 0;
    while (emitted < count) {
        size_t chunk = std::min<long>(count - emitted, (long)sizeof(buf));
        for (size_t i = 0; i < chunk; ++i) {
            buf[i] = 'a' + ((emitted + (long)i) % 26);
        }
        ssize_t written = write(STDOUT_FILENO, buf, chunk);
        if (written <= 0) {
            _exit(100);
        }
        emitted += written;
    }
    _exit(exitCode);
}

bool MatchesPattern(const std::string &s, size_t len) {
    // ExecCmdSync appends a trailing '\0' to non-empty output.
    if (s.size() != len + 1 || s.back() != '\0') {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        if (s[i] != 'a' + (char)(i % 26)) {
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char **argv) {
    if (argc >= 4 && strcmp(argv[1], "__child_emit") == 0) {
        return ChildEmitMain(argv[2], argv[3]);
    }

    // Avoid duplicated output when a forked child flushes the inherited
    // stdio buffer on exit().
    setvbuf(stdout, nullptr, _IONBF, 0);

    signal(SIGALRM, OnWatchdog);
    const char *self = argv[0];

    // 1. Regression: 4 MiB output (>> 64 KiB pipe buffer) must not deadlock
    //    and must be captured byte-for-byte.
    {
        ArmWatchdog();
        std::string out;
        int rc = ExecCmdSync(&out, self, "__child_emit", "4194304", "0");
        DisarmWatchdog();
        Check(rc == 0 && MatchesPattern(out, 4194304), "large output (4 MiB) captured without deadlock");
    }

    // 2. Small output is captured correctly.
    {
        ArmWatchdog();
        std::string out;
        int rc = ExecCmdSync(&out, self, "__child_emit", "5", "0");
        DisarmWatchdog();
        Check(rc == 0 && MatchesPattern(out, 5), "small output captured");
    }

    // 3. Empty output yields an empty string.
    {
        ArmWatchdog();
        std::string out = "pre-existing";
        int rc = ExecCmdSync(&out, self, "__child_emit", "0", "0");
        DisarmWatchdog();
        Check(rc == 0 && out.empty(), "empty output yields empty string");
    }

    // 4. Exit code is propagated to the caller.
    {
        ArmWatchdog();
        std::string out;
        int rc = ExecCmdSync(&out, self, "__child_emit", "1024", "42");
        DisarmWatchdog();
        Check(rc == 42 && MatchesPattern(out, 1024), "exit code propagated");
    }

    // 5. execv failure (bad binary) reports the child's exit(-1) == 255.
    {
        ArmWatchdog();
        std::string out;
        int rc = ExecCmdSync(&out, "/nonexistent/dfps-test-binary", "arg");
        DisarmWatchdog();
        Check(rc == 255, "exec failure returns 255");
    }

    // 6. nullptr content runs the command without a pipe and still reports
    //    the exit code.
    {
        ArmWatchdog();
        int rcOk = ExecCmdSync(nullptr, self, "__child_emit", "0", "0");
        int rcErr = ExecCmdSync(nullptr, self, "__child_emit", "0", "1");
        DisarmWatchdog();
        Check(rcOk == 0 && rcErr == 1, "nullptr content runs without pipe");
    }

    if (gFailures != 0) {
        printf("%d check(s) failed\n", gFailures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
