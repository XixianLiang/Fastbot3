#!/bin/zsh


./gradlew clean makeThirdPartJar
$ANDROID_HOME/build-tools/28.0.3/dx --dex --min-sdk-version=22 --output=kea2-thirdpart.jar $(pwd)/monkey/build/libs/kea2-thirdpart.jar
