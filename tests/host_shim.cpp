// Test-only compatibility shims for building the POSIX parts of misc.cpp
// on a macOS host. On Android/Linux the real libc symbols are used.
#if !defined(__linux__)

#include <fcntl.h>
#include <unistd.h>

extern "C" int pipe2(int pipefd[2], int flags) {
    if (pipe(pipefd) != 0) {
        return -1;
    }
    if (flags & O_CLOEXEC) {
        fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);
        fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);
    }
    if (flags & O_NONBLOCK) {
        fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
        fcntl(pipefd[1], F_SETFL, O_NONBLOCK);
    }
    return 0;
}

#endif
