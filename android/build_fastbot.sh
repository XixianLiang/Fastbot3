#!/usr/bin/env bash

# Top-level Fastbot Android build wrapper.
#
# Native crash/tombstone symbolication (llvm-addr2line, ndk-stack):
#   Default matches android/native/build_native.sh: FASTBOT_STRIP_NATIVE_SO=ON (stripped Release .so).
#   Keep debug symbols: FASTBOT_STRIP_NATIVE_SO=OFF ./build_fastbot.sh native
#
# Examples:
#   ./build_fastbot.sh all
#   ./build_fastbot.sh native                 # all four ABIs
#   ./build_fastbot.sh native arm64-v8a       # build arm64 only
#   FASTBOT_STRIP_NATIVE_SO=OFF ./build_fastbot.sh native arm64-v8a

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Explicit default to keep behavior consistent with docs; child process
# build_native.sh reads this variable as well.
export FASTBOT_STRIP_NATIVE_SO="${FASTBOT_STRIP_NATIVE_SO:-ON}"

build_native() {
  echo "==== Building native (FASTBOT_STRIP_NATIVE_SO=$FASTBOT_STRIP_NATIVE_SO) ===="
  (
    cd "$ROOT_DIR/native"
    ./build_native.sh "$@"
  )
}

build_monkeyq() {
  echo "==== Building monkeyq.jar (Gradle) ===="
  (
    cd "$ROOT_DIR"
    ./build_monkeyq.sh
  )
}

if [[ $# -eq 0 ]]; then
  CMD="all"
else
  CMD="$1"
  shift
fi

case "$CMD" in
  native)
    build_native "$@"
    ;;
  monkeyq)
    if [[ $# -gt 0 ]]; then
      echo "Warning: extra arguments ignored for monkeyq: $*" >&2
    fi
    build_monkeyq
    ;;
  all)
    build_native "$@"
    build_monkeyq
    ;;
  -h|--help|help)
    cat <<EOF
Usage: $0 [native|monkeyq|all] [abi ...]

  native [abi ...]   CMake/NDK libfastbot_native.so (see native/build_native.sh).
                     Default ABIs if none: armeabi-v7a arm64-v8a x86 x86_64.

  monkeyq            Gradle monkeyq.jar only.

  all [abi ...]      native (same ABI args) then monkeyq.

  Default: stripped Release .so (FASTBOT_STRIP_NATIVE_SO=ON).
    Symbolication: FASTBOT_STRIP_NATIVE_SO=OFF $0 native

  Env: NDK_ROOT required for native.
EOF
    exit 0
    ;;
  *)
    echo "Usage: $0 [native|monkeyq|all] [abi ...]  (try $0 --help)" >&2
    exit 1
    ;;
esac

echo "Done."

