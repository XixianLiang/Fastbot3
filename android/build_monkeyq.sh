#!/bin/zsh

set -e
INPUT_JAR="$(pwd)/monkey/build/libs/monkey.jar"
OUTPUT_JAR="$(pwd)/monkeyq.jar"

# Gradle 7.6 仅支持 Java 8–19；Java 21+ 会报 "Unsupported class file major version 69"
# 注意：java_home -v 17 表示「17 或更高」，会误选到 Java 25，故需从 -V 里挑 8–19 的 JDK
pick_jdk_8_19() {
  [ ! -x /usr/libexec/java_home ] && return 1
  /usr/libexec/java_home -V 2>&1 | while IFS= read -r line; do
    if [[ "$line" == *" - "* ]]; then
      path="${line##* - }"
      path="${path%%[[:space:]]*}"
      [[ -z "$path" || ! -d "$path" ]] && continue
      major=$("$path/bin/java" -version 2>&1 | head -1 | sed -n 's/.*version "\([0-9]*\)\..*/\1/p')
      if [[ -n "$major" && "$major" -ge 8 && "$major" -le 19 ]]; then
        echo "$path"
        break
      fi
    fi
  done
  return 1
}
JAVA_VER=$(java -version 2>&1 | head -1)
if echo "$JAVA_VER" | grep -qE '"2[1-9]|"3[0-9]'; then
  JDK_OK=$(pick_jdk_8_19)
  if [[ -n "$JDK_OK" ]]; then
    echo "Gradle 7.6 needs Java 8–19; using: $JDK_OK"
    export JAVA_HOME="$JDK_OK"
  else
    echo "Gradle 7.6 does not support Java 21+. No JDK 8–19 found."
    echo "Install JDK 17 (e.g. brew install openjdk@17), then run:"
    echo "  export JAVA_HOME=\$(/usr/libexec/java_home -v 17)"
    echo "  sh build_monkeyq.sh"
    echo "Or list JVMs: /usr/libexec/java_home -V"
    exit 1
  fi
fi

./gradlew clean makeJar

# 使用最新 build-tools（新版 SDK 已移除 dx，改用 d8）
BUILD_TOOLS_DIR=$(ls -1 "$ANDROID_HOME/build-tools" 2>/dev/null | sort -V | tail -1)
BUILD_TOOLS="$ANDROID_HOME/build-tools/$BUILD_TOOLS_DIR"
if [ -z "$BUILD_TOOLS_DIR" ] || [ ! -d "$BUILD_TOOLS" ]; then
  echo "Error: ANDROID_HOME/build-tools not found. Set ANDROID_HOME to your Android SDK root."
  exit 1
fi

if [ -x "$BUILD_TOOLS/d8" ]; then
  # d8：新版 SDK 使用（dx 已移除）
  "$BUILD_TOOLS/d8" --min-api 22 --output "$OUTPUT_JAR" "$INPUT_JAR"
elif [ -x "$BUILD_TOOLS/dx" ]; then
  # dx：旧版 build-tools 备用
  "$BUILD_TOOLS/dx" --dex --min-sdk-version=22 --output="$OUTPUT_JAR" "$INPUT_JAR"
else
  echo "Error: Neither d8 nor dx found in $BUILD_TOOLS"
  exit 1
fi
echo "Built: $OUTPUT_JAR"
