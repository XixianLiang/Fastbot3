# Fastbot-Android Open Source Handbook

## Introduction

> Fastbot3 is a model-based testing tool for modeling GUI transitions to discover app stability problems. It combines machine learning and reinforcement learning techniques to assist discovery in a more intelligent way.


***More detail see at [Fastbot architecture](https://mp.weixin.qq.com/s/QhzqBFZygkIS6C69__smyQ)

## Features

* Compatible with the latest Android systems (Android 5–14 and beyond), including stock and manufacturer ROMs.
* **High speed**: state build ~0.1-0.5 ms, action decision ~50 ms per click; supports up to ~12 actions per second.
* Expert system supports deep customization for different business needs.
* **Reinforcement learning (RL)**: model-based testing with graph transition and high-reward action selection (e.g. Double SARSA).

### Reuse model (FBM)

* **Path**: On device, the reuse model file is stored at `/sdcard/fastbot3_{packagename}.fbm` (e.g. `/sdcard/fastbot3_com.example.app.fbm`).
* **Loading**: If this file exists when Fastbot starts, it is loaded by default for the given package.
* **Saving**: During execution, the model is written back periodically (e.g. every 10 minutes). You can delete or copy the file as needed.
* **Security**: The loader verifies the buffer before deserializing; invalid or corrupted files are rejected.

### Changelog

**update 2026.1**
* **Double SARSA**: Default agent is now **DoubleSarsaAgent**, implementing N-step Double SARSA with two Q-functions (Q1, Q2) to reduce overestimation bias and improve action selection stability (see [DOUBLE_SARSA_ALGORITHM_EXPLANATION.md](./native/agent/DOUBLE_SARSA_ALGORITHM_EXPLANATION.md)).
* **Dynamic state abstraction**: State granularity is tuned at runtime—finer when the same action leads to different screens or too many choices, coarser when states are over-split (see [STATE_ABSTRACTION_TECHNICAL_ARCHIVE.md](./native/desc/STATE_ABSTRACTION_TECHNICAL_ARCHIVE.md)).
* **Performance & security**: FBM loader verifies buffer before deserialization; activity name length is capped when serializing; KeyCompareLessThan and related reuse-model code hardened for null-safety and format.
* **XXH64**: Introduced xxHash 64-bit for fast state/action hashing in native layer.

**update 2023.9**
* Add Fastbot code analysis file for quick understanding of the source code. See [fastbot_code_analysis.md](./fastbot_code_analysis.md).

**update 2023.8**
* Java & C++ code are fully open-sourced (supported by and collaborated with [Prof. Ting Su](https://mobile-app-analysis.github.io/)'s research group from East China Normal University). Welcome code or idea contributions!

**update 2023.3**
* Support Android 13.

**update 2022.1**
* Update Fastbot Revised License.

**update 2021.11**
* Support Android 12.
* New GUI fuzzing & mutation features (inspired/supported by [Themis](https://github.com/the-themis-benchmarks/home)).

**update 2021.09**
* Fastbot supports model reuse: `/sdcard/fastbot_{packagename}.fbm`. Loaded by default when present; overwritten periodically during runs.

---

## Build

### Prerequisites

* **Java**: Gradle 7.6 supports **Java 8–19** only. If you see `Unsupported class file major version 69`, your JDK is too new (e.g. Java 25). Use JDK 17 or 11: `export JAVA_HOME=$(/usr/libexec/java_home -v 17)` (macOS), or install OpenJDK 17 and point `JAVA_HOME` to it.
* **Gradle**: Required for building the Java part. Recommended to manage versions via [sdkman](https://sdkman.io/):
  ```shell
  curl -s "https://get.sdkman.io" | bash
  sdk install gradle 7.6.2
  gradle wrapper
  ```
* **NDK & CMake**: Required for building the C++ native (.so) part. You need to set **NDK_ROOT** (or **ANDROID_NDK_HOME**) to your NDK path; the build script uses `$NDK_ROOT/build/cmake/android.toolchain.cmake`. Recommended: NDK r21 or later. For step-by-step installation (Android Studio, sdkmanager, or manual), see **[NDK 安装指南 / NDK Install Guide](./native/NDK_INSTALL_GUIDE.md)**. If you see *CMake 'X.Y.Z' was not found in SDK*, either install that version via SDK Manager → SDK Tools → CMake (e.g. 3.22.1 or 3.18.1), or set the same version in `monkey/build.gradle` → `externalNativeBuild.cmake.version` to match the CMake version you have installed.

### Build Java (monkeyq.jar)

From the **android** project root:

```shell
./gradlew clean makeJar
```

Then generate the dex jar. Newer SDKs use **d8** (dx was removed); the script auto-detects:

```shell
# Script uses latest build-tools and d8 (or dx if present)
sh build_monkeyq.sh
```

Manual step (use **d8** in recent build-tools, or **dx** in older ones):

```shell
# d8 (recommended when dx is not in your build-tools)
$ANDROID_HOME/build-tools/<version>/d8 --min-api 22 --output=monkeyq.jar monkey/build/libs/monkey.jar
# or dx (older SDK)
$ANDROID_HOME/build-tools/<version>/dx --dex --min-sdk-version=22 --output=monkeyq.jar monkey/build/libs/monkey.jar
```

Or use the helper script (preferred):

```shell
sh build_monkeyq.sh
```

The result is `monkeyq.jar` in the project root (or as specified by the script).

### Build native (.so)

The native C++ code is built with CMake and the Android NDK. **Prerequisites**: NDK and CMake installed, and **NDK_ROOT** set (see [NDK Install Guide](./native/NDK_INSTALL_GUIDE.md)).

From the **android** project root:

```shell
cd native
sh ./build_native.sh
```

Or in one line: `cd native && sh ./build_native.sh`.

The script builds for **armeabi-v7a**, **arm64-v8a**, **x86**, and **x86_64**. After a successful build, the `.so` files are under **android/libs/** (e.g. `libs/arm64-v8a/libfastbot_native.so`). Push this directory to the device when deploying (see [Usage](#usage)).

---

## Usage

### Environment preparation

1. Build artifacts (see [Build](#build)):
   ```shell
   ./gradlew clean makeJar
   $ANDROID_HOME/build-tools/28.0.3/dx --dex --min-sdk-version=22 --output=monkeyq.jar monkey/build/libs/monkey.jar
   cd native && sh build_native.sh
   ```

2. Push artifacts to the device:
   ```shell
   adb push monkeyq.jar /sdcard/monkeyq.jar
   adb push fastbot-thirdpart.jar /sdcard/fastbot-thirdpart.jar
   adb push libs/* /data/local/tmp/
   adb push framework.jar /sdcard/framework.jar
   ```

### Run Fastbot (shell)

```shell
adb -s <device_serial> shell CLASSPATH=/sdcard/monkeyq.jar:/sdcard/framework.jar:/sdcard/fastbot-thirdpart.jar exec app_process /system/bin com.android.commands.monkey.Monkey -p <package_name> --agent reuseq --running-minutes <duration_min> --throttle <delay_ms> -v -v
```

* Before running, you can push valid strings from the APK to `/sdcard` to improve the model (use `aapt2` or `aapt` from your Android SDK, e.g. `${ANDROID_HOME}/build-tools/28.0.2/aapt2`):
  ```shell
  aapt2 dump --values strings <testApp_path.apk> > max.valid.strings
  adb push max.valid.strings /sdcard
  ```

> For more details, see [中文手册](./handbook-cn.md).

#### Required parameters

| Parameter | Description |
|-----------|-------------|
| `-s <device_serial>` | Device ID (required if multiple devices are connected; check with `adb devices`) |
| `-p <package_name>` | App package under test (e.g. from `adb shell pm list packages`) |
| `--agent reuseq` | Strategy: Double SARSA agent (recommended) |
| `--running-minutes <duration>` | Total test duration in minutes |
| `--throttle <delay_ms>` | Delay between actions in milliseconds |

#### Optional parameters

| Parameter | Description |
|-----------|-------------|
| `--bugreport` | Include bug report when a crash occurs |
| `--output-directory <path>` | Output directory (e.g. `/sdcard/xxx`) |

#### Optional fuzzing data

```shell
adb push data/fuzzing/ /sdcard/
adb shell am broadcast -a android.intent.action.MEDIA_SCANNER_SCAN_FILE -d file:///sdcard/fuzzing
```

### Results

#### Crashes and ANR

* Java crashes, ANR, and native crashes are logged to `/sdcard/crash-dump.log`.
* ANR traces are written to `/sdcard/oom-traces.log`.

#### Activity coverage

* After the run, the shell prints the total activity list, explored activities, and coverage rate.
* **Coverage** = exploredActivity / totalActivity × 100%.
* **Note**: `totalActivity` comes from `PackageManager.getPackageInfo` and may include unused, invisible, or unreachable activities.

---

## Code structure and extension

### Layout

* **Java**: `monkey/` — device interaction, local server, and passing GUI info to native.
* **Native (C++)**: `native/` — reward computation and action selection; returns the next action to the client as an Operate object (e.g. JSON).

For a guided tour of the codebase, see [fastbot_code_analysis.md](./fastbot_code_analysis.md).

### Extending Fastbot

You can extend both the Java and C++ layers. Refer to the code analysis document and the handbook for details.

---

## Acknowledgement

* We thank Prof. Ting Su (East China Normal University), Dr. Tianxiao Gu, Prof. Zhendong Su (ETH Zurich), and others for insights and code contributions.
* We thank Prof. Yao Guo (PKU) for useful discussions on Fastbot.
* We thank Prof. Zhenhua Li (THU) and Dr. Liangyi Gong (THU) for their helpful opinions on Fastbot.
* We thank Prof. Jian Zhang (Chinese Academy of Sciences) for valuable advice.

---

## Publications

If you use this work in your research, please cite:

1. Lv, Zhengwei, Chao Peng, Zhao Zhang, Ting Su, Kai Liu, Ping Yang (2022). “Fastbot2: Reusable Automated Model-based GUI Testing for Android Enhanced by Reinforcement Learning”. In Proceedings of the 37th IEEE/ACM International Conference on Automated Software Engineering (ASE 2022). [[pdf]](https://se-research.bytedance.com/pdf/ASE22.pdf)

```bibtex
@inproceedings{fastbot2,
  title={Fastbot2: Reusable Automated Model-based GUI Testing for Android Enhanced by Reinforcement Learning},
  author={Lv, Zhengwei and Peng, Chao and Zhang, Zhao and Su, Ting and Liu, Kai and Yang, Ping},
  booktitle={Proceedings of the 37th IEEE/ACM International Conference on Automated Software Engineering (ASE 2022)},
  year={2022}
}
```

2. Peng, Chao, Zhao Zhang, Zhengwei Lv, Ping Yang (2022). “MUBot: Learning to Test Large-Scale Commercial Android Apps like a Human”. In Proceedings of the 38th International Conference on Software Maintenance and Evolution (ICSME 2022). [[pdf]](https://se-research.bytedance.com/pdf/ICSME22B.pdf)

```bibtex
@inproceedings{mubot,
  title={MUBot: Learning to Test Large-Scale Commercial Android Apps like a Human},
  author={Peng, Chao and Zhang, Zhao and Lv, Zhengwei and Yang, Ping},
  booktitle={Proceedings of the 38th International Conference on Software Maintenance and Evolution (ICSME 2022)},
  year={2022}
}
```

3. Cai, Tianqin, Zhao Zhang, and Ping Yang. “Fastbot: A Multi-Agent Model-Based Test Generation System”. In Proceedings of the IEEE/ACM 1st International Conference on Automation of Software Test. 2020. [[pdf]](https://se-research.bytedance.com/pdf/AST20.pdf)

```bibtex
@inproceedings{fastbot,
  title={Fastbot: A Multi-Agent Model-Based Test Generation System},
  author={Cai, Tianqin and Zhang, Zhao and Yang, Ping},
  booktitle={Proceedings of the IEEE/ACM 1st International Conference on Automation of Software Test},
  pages={93--96},
  year={2020}
}
```

---

## Contributors

Zhao Zhang, Jianqiang Guo, Yuhui Su, Tianxiao Gu, Zhengwei Lv, Tianqin Cai, Chao Peng, Bao Cao, Shanshan Shao, Dingchun Wang, Jiarong Fu, Ping Yang, Ting Su, Mengqian Xu

We welcome more contributors.
