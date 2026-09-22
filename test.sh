#!/bin/bash
#
# Manual unit-test runner for dfps.
#
# Usage:
#   ./test.sh            Build and run the unit tests on this host (POSIX).
#   ./test.sh host       Same as above.
#   ./test.sh device     Cross-compile with $ANDROID_NDK and run on an
#                        adb-connected device (root recommended, not needed
#                        for these tests).
#
# Each test binary is executed under a watchdog: a regression that
# re-introduces the ExecCmdSync() pipe deadlock is killed after the timeout
# instead of hanging the runner forever.
#
set -euo pipefail

BASEDIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$BASEDIR/build-tests"
HOST_BIN="$BUILD_DIR/test_exec_cmd_sync"
DEVICE_BIN_NAME="dfps_exec_cmd_sync_test"
DEVICE_BIN="$BUILD_DIR/$DEVICE_BIN_NAME"
DEVICE_PATH="/data/local/tmp/$DEVICE_BIN_NAME"
WATCHDOG_SECS=30

mkdir -p "$BUILD_DIR"

# $1 timeout seconds, remaining args: command to run
run_with_timeout() {
    local secs=$1
    shift
    "$@" &
    local pid=$!
    (
        sleep "$secs"
        kill -9 "$pid" 2>/dev/null
    ) &
    local watcher=$!
    set +e
    wait "$pid"
    local rc=$?
    set -e
    kill "$watcher" 2>/dev/null || true
    wait "$watcher" 2>/dev/null || true
    if [[ $rc -eq 137 ]]; then
        echo " ! TIMEOUT after ${secs}s (deadlock regression?)" >&2
    fi
    return $rc
}

find_host_cxx() {
    if [[ -n "${CXX:-}" ]]; then
        echo "$CXX"
        return
    fi
    for c in clang++ g++ c++; do
        if command -v "$c" >/dev/null 2>&1; then
            echo "$c"
            return
        fi
    done
    echo " ! No C++ compiler found (set \$CXX)" >&2
    exit 1
}

build_host() {
    local cxx
    cxx=$(find_host_cxx)
    echo ">>> Building host unit tests with $cxx"
    "$cxx" -std=c++17 -O1 -g -D_GNU_SOURCE -Wall -Wextra \
        -I"$BASEDIR/source" \
        -I"$BASEDIR/thirdparty/spdlog" \
        -I"$BASEDIR/thirdparty/scnlib/include" \
        -I"$BASEDIR/tests/host_shim/include" \
        "$BASEDIR/source/utils/misc.cpp" \
        "$BASEDIR/tests/host_shim.cpp" -include "$BASEDIR/tests/host_shim/include/host_compat.h" \
        "$BASEDIR/tests/test_exec_cmd_sync.cpp" \
        -o "$HOST_BIN"
}

run_host() {
    echo ">>> Running host unit tests (watchdog ${WATCHDOG_SECS}s)"
    run_with_timeout "$WATCHDOG_SECS" "$HOST_BIN"
}

find_ndk_cxx() {
    if [[ -z "${ANDROID_NDK:-}" ]]; then
        echo " ! \$ANDROID_NDK is not set" >&2
        exit 1
    fi
    local cxx
    cxx=$(ls "$ANDROID_NDK"/toolchains/llvm/prebuilt/*/bin/aarch64-linux-android23-clang++ 2>/dev/null | head -n1 || true)
    if [[ -z "$cxx" ]]; then
        echo " ! aarch64-linux-android23-clang++ not found under \$ANDROID_NDK" >&2
        exit 1
    fi
    echo "$cxx"
}

build_device() {
    local cxx
    cxx=$(find_ndk_cxx)
    echo ">>> Building device unit tests with $cxx"
    "$cxx" -std=c++17 -O1 -g -D_GNU_SOURCE -Wall -Wextra -static-libstdc++ \
        -I"$BASEDIR/source" \
        -I"$BASEDIR/thirdparty/spdlog" \
        -I"$BASEDIR/thirdparty/scnlib/include" \
        "$BASEDIR/source/utils/misc.cpp" \
        "$BASEDIR/tests/test_exec_cmd_sync.cpp" \
        -o "$DEVICE_BIN"
}

run_device() {
    echo ">>> Pushing unit tests to device ($DEVICE_PATH)"
    adb push "$DEVICE_BIN" "$DEVICE_PATH" >/dev/null
    adb shell chmod 755 "$DEVICE_PATH"
    echo ">>> Running device unit tests (watchdog ${WATCHDOG_SECS}s)"
    run_with_timeout "$WATCHDOG_SECS" \
        adb shell "$DEVICE_PATH; echo TEST_RC=\$?"
}

mode="${1:-host}"
case "$mode" in
host)
    build_host
    run_host
    ;;
device)
    build_device
    run_device
    ;;
*)
    echo " ! Unknown mode '$mode' (use 'host' or 'device')" >&2
    exit 1
    ;;
esac

echo ">>> All unit tests passed"
