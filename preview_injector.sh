#!/usr/bin/env bash
# Compiles Injector-c and launches the real app window so you can click around and exercise it
# yourself. Run the unit test suite separately with `ctest --test-dir Injector-c/build` if you
# just want the automated checks.
#
# Usage: ./preview_injector.sh [--clean] [-j N]
#   --clean   wipe Injector-c/build first (use after changing CMakeLists.txt structure, or if
#             the cache gets into a weird state)
#   -j N      parallel build jobs (default: number of CPUs)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$HERE/Injector-c"
BUILD_DIR="$SRC_DIR/build"
JOBS=""
CLEAN=0

while [ $# -gt 0 ]; do
    case "$1" in
        --clean) CLEAN=1; shift ;;
        -j) JOBS="$2"; shift 2 ;;
        -j*) JOBS="${1#-j}"; shift ;;
        -h|--help)
            sed -n '2,9p' "$0"
            exit 0
            ;;
        *) echo "Unknown argument: $1" >&2; exit 1 ;;
    esac
done

if [ -z "$JOBS" ]; then
    if command -v nproc >/dev/null 2>&1; then
        JOBS="$(nproc)"
    elif command -v sysctl >/dev/null 2>&1; then
        JOBS="$(sysctl -n hw.ncpu)"
    else
        JOBS=4
    fi
fi

if [ "$CLEAN" = "1" ]; then
    echo "==> Removing $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

echo "==> Configuring (cmake -S $SRC_DIR -B $BUILD_DIR)"
cmake -S "$SRC_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug

echo "==> Building (-j $JOBS)"
cmake --build "$BUILD_DIR" -j "$JOBS"

INJECTOR_BIN="$BUILD_DIR/injector"
if [ ! -x "$INJECTOR_BIN" ]; then
    echo "FAIL: $INJECTOR_BIN not found or not executable" >&2
    exit 1
fi

echo "==> Launching the app (close the window when you're done)"
"$INJECTOR_BIN"
