#!/bin/zsh

# Script dir for relative paths (e.g. ADBKeyBoard.apk)
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

adb push monkeyq.jar /sdcard/monkeyq.jar
adb push kea2-thirdpart.jar /sdcard/kea2-thirdpart.jar
adb push fastbot-thirdpart.jar /sdcard/fastbot-thirdpart.jar
adb push libs/* /data/local/tmp/
adb push framework.jar /sdcard/framework.jar

# Integrate ADBKeyBoard: push APK, install, enable and set as IME (for Chinese/emoji input).
# Try keyboardservice-debug.apk first (higher SDK); if missing or install fails, try ADBKeyBoard.apk.
ADBKEYBOARD_INSTALLED=0
if [ -f "$SCRIPT_DIR/test/keyboardservice-debug.apk" ]; then
  adb push "$SCRIPT_DIR/test/keyboardservice-debug.apk" /data/local/tmp/ADBKeyBoard.apk
  if adb shell pm install -r /data/local/tmp/ADBKeyBoard.apk; then
    ADBKEYBOARD_INSTALLED=1
  fi
fi
if [ "$ADBKEYBOARD_INSTALLED" -eq 0 ] && [ -f "$SCRIPT_DIR/test/ADBKeyBoard.apk" ]; then
  adb push "$SCRIPT_DIR/test/ADBKeyBoard.apk" /data/local/tmp/ADBKeyBoard.apk
  if adb shell pm install -r /data/local/tmp/ADBKeyBoard.apk; then
    ADBKEYBOARD_INSTALLED=1
  fi
fi
if [ "$ADBKEYBOARD_INSTALLED" -eq 1 ]; then
  adb shell ime enable com.android.adbkeyboard/.AdbIME
  adb shell ime set com.android.adbkeyboard/.AdbIME
fi

adb shell CLASSPATH='/sdcard/monkeyq.jar:/sdcard/framework.jar:/sdcard/fastbot-thirdpart.jar:/sdcard/kea2-thirdpart.jar' exec app_process /system/bin com.android.commands.monkey.Monkey -p com.luna.music --agent reuseq --running-minutes 10 --throttle 10 -v -v -v
