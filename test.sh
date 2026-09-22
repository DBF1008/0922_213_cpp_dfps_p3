#!/bin/bash
#
# Build and run host-side unit tests for dfps on a dev machine (macOS/Linux).
# Each test/<name>_test.cpp is compiled together with the production sources
# it exercises and executed with a watchdog-guarded timeout.

set -u

BASEDIR="$(cd "$(dirname "$0")" && pwd)"
TEST_DIR="$BASEDIR/test"
BUILD_DIR="$BASEDIR/build/host-test"
CXX="${CXX:-c++}"

mkdir -p "$BUILD_DIR"

COMMON_FLAGS=(
    -std=c++17
    -Wall
    -Wextra
    -g
    -O1
    -D_GNU_SOURCE
    -I"$BASEDIR/source/utils"
    -I"$BASEDIR/thirdparty/spdlog"
    -I"$BASEDIR/thirdparty/scnlib/include"
    -I"$TEST_DIR/compat"
    -include "$TEST_DIR/compat/compat.h"
)

failures=0
for src in "$TEST_DIR"/*_test.cpp; do
    name="$(basename "$src" .cpp)"
    bin="$BUILD_DIR/$name"

    echo ">>> Building $name"
    if ! "$CXX" "${COMMON_FLAGS[@]}" "$src" "$BASEDIR/source/utils/misc.cpp" -o "$bin" -lpthread; then
        echo " ! Build failed: $name"
        failures=$((failures + 1))
        continue
    fi

    echo ">>> Running $name"
    if "$bin"; then
        echo ">>> $name: OK"
    else
        echo " ! $name: FAILED (exit $?)"
        failures=$((failures + 1))
    fi
done

if [ "$failures" -ne 0 ]; then
    echo " ! $failures test(s) failed"
    exit 1
fi
echo ">>> All tests passed"
