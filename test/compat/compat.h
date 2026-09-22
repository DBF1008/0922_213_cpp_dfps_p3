#pragma once

// Compatibility shims force-included (-include) when building dfps sources
// for host-side unit tests on macOS.

#ifdef __APPLE__

#include <fcntl.h>
#include <unistd.h>

// macOS has no pipe2(); emulate the flags used by misc.cpp (O_CLOEXEC).
static inline int pipe2(int fds[2], int flags) {
    if (pipe(fds) != 0) {
        return -1;
    }
    if (flags & O_CLOEXEC) {
        fcntl(fds[0], F_SETFD, FD_CLOEXEC);
        fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    }
    if (flags & O_NONBLOCK) {
        fcntl(fds[0], F_SETFL, O_NONBLOCK);
        fcntl(fds[1], F_SETFL, O_NONBLOCK);
    }
    return 0;
}

#endif // __APPLE__
