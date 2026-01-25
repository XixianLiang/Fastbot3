#!/bin/zsh

adb push monkeyq.jar /sdcard/monkeyq.jar
adb push kea2-thirdpart.jar /sdcard/kea2-thirdpart.jar
adb push fastbot-thirdpart.jar /sdcard/fastbot-thirdpart.jar
adb push libs/* /data/local/tmp/
adb push framework.jar /sdcard/framework.jar


adb shell CLASSPATH="/sdcard/monkeyq.jar:/sdcard/framework.jar:/sdcard/fastbot-thirdpart.jar:/sdcard/kea2-thirdpart.jar" exec app_process /system/bin com.android.commands.monkey.Monkey -p com.luna.music --agent reuseq --running-minutes 10 --throttle 10 -v -v -v
