# Android NDK 安装指南

## 方法 1：通过 Android Studio 安装（推荐）

### 步骤：

1. **下载并安装 Android Studio**
   - 访问：https://developer.android.com/studio
   - 下载 macOS 版本并安装

2. **在 Android Studio 中安装 NDK**
   - 打开 Android Studio
   - 进入 **Preferences** (⌘,) → **Appearance & Behavior** → **System Settings** → **Android SDK**
   - 切换到 **SDK Tools** 标签页
   - 勾选以下选项：
     - ✅ **NDK (Side by side)** - 这是 NDK 本身
     - ✅ **CMake** - 用于构建 C/C++ 代码
   - 点击 **Apply** 开始下载安装

3. **查找 NDK 安装路径**
   安装完成后，NDK 通常位于：
   ```
   ~/Library/Android/sdk/ndk/<version>
   ```
   例如：`~/Library/Android/sdk/ndk/25.2.9519653`

4. **设置环境变量**
   在 `~/.zshrc` 或 `~/.bash_profile` 中添加：
   ```bash
   # 设置 Android SDK 路径
   export ANDROID_HOME=$HOME/Library/Android/sdk
   export PATH=$PATH:$ANDROID_HOME/tools:$ANDROID_HOME/platform-tools
   
   # 设置 NDK 路径（替换 <version> 为实际版本号）
   export NDK_ROOT=$HOME/Library/Android/sdk/ndk/<version>
   # 或者使用最新的 NDK 版本
   export NDK_ROOT=$HOME/Library/Android/sdk/ndk/$(ls -1 $HOME/Library/Android/sdk/ndk | sort -V | tail -1)
   ```

5. **使环境变量生效**
   ```bash
   source ~/.zshrc
   ```

---

## 方法 2：使用命令行工具安装

### 前提条件：
需要先安装 Android SDK Command Line Tools

### 步骤：

1. **下载 Command Line Tools**
   ```bash
   # 创建 SDK 目录
   mkdir -p ~/Library/Android/sdk
   
   # 下载 Command Line Tools (macOS)
   cd ~/Library/Android/sdk
   curl -o cmdline-tools.zip https://dl.google.com/android/repository/commandlinetools-mac-11076708_latest.zip
   unzip cmdline-tools.zip
   mkdir -p cmdline-tools
   mv cmdline-tools/* cmdline-tools/latest/
   ```

2. **安装 NDK**
   ```bash
   # 设置环境变量
   export ANDROID_HOME=$HOME/Library/Android/sdk
   export PATH=$PATH:$ANDROID_HOME/cmdline-tools/latest/bin
   
   # 接受许可证并安装 NDK
   yes | sdkmanager --licenses
   sdkmanager "ndk;25.2.9519653" "cmake;3.22.1"
   
   # 或者安装最新版本
   sdkmanager "ndk-bundle" "cmake"
   ```

3. **设置 NDK_ROOT**
   ```bash
   # 找到已安装的 NDK 版本
   ls ~/Library/Android/sdk/ndk/
   
   # 设置环境变量（替换为实际版本号）
   export NDK_ROOT=$HOME/Library/Android/sdk/ndk/25.2.9519653
   ```

---

## 方法 3：手动下载安装

1. **下载 NDK**
   - 访问：https://developer.android.com/ndk/downloads
   - 下载适合 macOS 的版本

2. **解压并设置**
   ```bash
   # 解压到指定目录
   unzip android-ndk-r25c-darwin.zip -d ~/Library/Android/sdk/
   mv ~/Library/Android/sdk/android-ndk-r25c ~/Library/Android/sdk/ndk/25.2.9519653
   
   # 设置环境变量
   export NDK_ROOT=$HOME/Library/Android/sdk/ndk/25.2.9519653
   ```

---

## 验证安装

运行以下命令验证 NDK 是否正确安装：

```bash
# 检查 NDK_ROOT 是否设置
echo $NDK_ROOT

# 检查工具链文件是否存在
ls $NDK_ROOT/build/cmake/android.toolchain.cmake

# 检查 CMake 是否可用
cmake --version
```

如果所有检查都通过，就可以运行构建脚本了：

```bash
cd native
sh ./build_native.sh
```

---

## 常见问题

### Q: 如何查看已安装的 NDK 版本？
```bash
ls ~/Library/Android/sdk/ndk/
```

### Q: 需要安装哪个版本的 NDK？
- 推荐使用 **NDK r21 或更高版本**（支持 CMake）
- 最新稳定版本通常是最好的选择

### Q: 如何更新 NDK？
在 Android Studio 中：
- Preferences → Android SDK → SDK Tools
- 取消勾选旧版本，勾选新版本
- 点击 Apply

### Q: 构建时提示找不到工具链文件？
确保：
1. `NDK_ROOT` 环境变量已正确设置
2. NDK 版本支持 CMake（r21+）
3. 工具链文件路径正确：`$NDK_ROOT/build/cmake/android.toolchain.cmake`

---

## 快速设置脚本

将以下内容添加到 `~/.zshrc` 可以自动设置最新的 NDK：

```bash
# Android SDK 配置
export ANDROID_HOME=$HOME/Library/Android/sdk
export PATH=$PATH:$ANDROID_HOME/tools:$ANDROID_HOME/platform-tools

# 自动设置最新的 NDK 版本
if [ -d "$ANDROID_HOME/ndk" ]; then
    LATEST_NDK=$(ls -1 $ANDROID_HOME/ndk | sort -V | tail -1)
    if [ -n "$LATEST_NDK" ]; then
        export NDK_ROOT=$ANDROID_HOME/ndk/$LATEST_NDK
    fi
fi
```

然后运行：
```bash
source ~/.zshrc
```
