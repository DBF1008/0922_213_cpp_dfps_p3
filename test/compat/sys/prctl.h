#pragma once

// Host-build stub for the Linux-only <sys/prctl.h>.
// misc.cpp only uses prctl(PR_SET_NAME, ...) in SetSelfThreadName(), which is
// irrelevant for the host-side unit tests, so a no-op stub is sufficient.

#define PR_SET_NAME 15

static inline int prctl(int option, ...) {
    (void)option;
    return 0;
}
