#!/usr/bin/env bash
# Build libfastbot_native.so for Android ABIs (run from repo: no need to cd manually).
#
# Prerequisites: export NDK_ROOT=/path/to/ndk
#
# Crash / tombstone symbolication (llvm-addr2line, ndk-stack):
#   Default FASTBOT_STRIP_NATIVE_SO=OFF -> Release + -g, unstripped .so under android/libs/<ABI>/
#   Smaller APK: FASTBOT_STRIP_NATIVE_SO=ON ./build_native.sh
#
# Examples:
#   ./build_native.sh                          # all ABIs (armeabi-v7a, arm64-v8a, x86, x86_64)
#   ./build_native.sh arm64-v8a               # one ABI (fast iteration)
#   ./build_native.sh arm64-v8a armeabi-v7a   # subset

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [[ -z "${NDK_ROOT:-}" ]]; then
    echo "Error: NDK_ROOT is not set." >&2
    echo "  export NDK_ROOT=\$HOME/Library/Android/sdk/ndk/<version>" >&2
    echo "  or: export NDK_ROOT=\$ANDROID_NDK_HOME" >&2
    exit 1
fi

# Check if the toolchain file exists
TOOLCHAIN_FILE="$NDK_ROOT/build/cmake/android.toolchain.cmake"
if [[ ! -f "$TOOLCHAIN_FILE" ]]; then
    echo "Error: toolchain not found: $TOOLCHAIN_FILE" >&2
    exit 1
fi

# OFF = keep symbols for addr2line/ndk-stack (see CMakeLists FASTBOT_STRIP_NATIVE_SO).
STRIP_OPT="${FASTBOT_STRIP_NATIVE_SO:-OFF}"

JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

DEFAULT_ABIS=(armeabi-v7a arm64-v8a x86 x86_64)
if [[ $# -gt 0 ]]; then
    ABIS=("$@")
else
    ABIS=("${DEFAULT_ABIS[@]}")
fi

build_abi() {
    local ABI=$1
    echo "=== Building $ABI (FASTBOT_STRIP_NATIVE_SO=$STRIP_OPT) ==="

    rm -rf CMakeFiles/ CMakeCache.txt cmake_install.cmake Makefile 2>/dev/null || true

    cmake -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
          -DANDROID_ABI="$ABI" \
          -DCMAKE_BUILD_TYPE=Release \
          -DFASTBOT_STRIP_NATIVE_SO="$STRIP_OPT" \
          .

    make -j"$JOBS"

    local OUT="../libs/$ABI/libfastbot_native.so"
    if [[ -f "$OUT" ]]; then
        echo "OK: $OUT ($(wc -c < "$OUT" | tr -d ' ') bytes)"
    else
        echo "Warning: expected output not found: $OUT" >&2
    fi
}

for abi in "${ABIS[@]}"; do
    build_abi "$abi"
done

echo "All requested builds completed: ${ABIS[*]}"
