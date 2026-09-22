#pragma once

// Test-only: expose a pipe2() declaration when building misc.cpp on a
// macOS host (glibc/bionic declare it in <unistd.h>, macOS does not).
#if !defined(__linux__)
#include <fcntl.h>
extern "C" int pipe2(int pipefd[2], int flags);
#endif
