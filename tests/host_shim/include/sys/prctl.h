#pragma once

// Test-only shim: production code includes <sys/prctl.h>, which does not
// exist on macOS hosts used to run the unit-test build locally.
#if defined(__linux__)
#include_next <sys/prctl.h>
#else
#define PR_SET_NAME 15
static inline int prctl(int, ...) { return 0; }
#endif
