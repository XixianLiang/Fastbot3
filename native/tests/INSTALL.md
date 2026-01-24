# 单元测试安装和运行指南

## 前置要求

### 1. 安装 CMake

**macOS:**
```bash
brew install cmake
```

**Linux (Ubuntu/Debian):**
```bash
sudo apt-get update
sudo apt-get install cmake
```

**Linux (CentOS/RHEL):**
```bash
sudo yum install cmake
```

### 2. 安装 C++ 编译器

**macOS:**
```bash
# 通常已预装 Xcode Command Line Tools
xcode-select --install
```

**Linux:**
```bash
sudo apt-get install build-essential  # Ubuntu/Debian
sudo yum groupinstall "Development Tools"  # CentOS/RHEL
```

### 3. 安装 Google Test（可选）

CMakeLists.txt 会自动下载 Google Test，但也可以手动安装：

**macOS:**
```bash
brew install googletest
```

**Linux (Ubuntu/Debian):**
```bash
sudo apt-get install libgtest-dev
```

## 构建和运行测试

### 步骤 1: 进入测试目录

```bash
cd /Users/zza/Documents/GitHub/Fastbot3/native/tests
```

### 步骤 2: 创建构建目录

```bash
mkdir -p build
cd build
```

### 步骤 3: 运行 CMake

```bash
cmake ..
```

### 步骤 4: 编译

```bash
make -j4
```

### 步骤 5: 运行测试

```bash
./fastbot_tests
```

## 快速脚本

创建 `run_tests.sh`:

```bash
#!/bin/bash
cd "$(dirname "$0")"
mkdir -p build
cd build
cmake .. && make -j4 && ./fastbot_tests
```

然后运行:
```bash
chmod +x run_tests.sh
./run_tests.sh
```

## 常见问题

### CMake 未找到

如果 `cmake` 命令未找到，请确保：
1. 已安装 CMake
2. CMake 在 PATH 中

检查：
```bash
which cmake
cmake --version
```

### Google Test 下载失败

如果网络问题导致 Google Test 下载失败：
1. 手动下载并安装 Google Test
2. 或使用代理

### 编译错误

如果遇到编译错误：
1. 检查 C++ 编译器版本（需要支持 C++14）
2. 检查所有依赖是否已安装
3. 查看错误信息并修复

### 链接错误

如果遇到链接错误：
1. 确保所有源文件都在 CMakeLists.txt 中
2. 检查库路径是否正确

## 生成覆盖率报告

### 安装 lcov (可选)

**macOS:**
```bash
brew install lcov
```

**Linux:**
```bash
sudo apt-get install lcov
```

### 生成报告

```bash
cd build
./fastbot_tests
lcov --capture --directory . --output-file coverage.info
lcov --remove coverage.info '/usr/*' '*/thirdpart/*' --output-file coverage.info
genhtml coverage.info --output-directory coverage_report
open coverage_report/index.html  # macOS
```

## 验证安装

运行以下命令验证环境：

```bash
# 检查 CMake
cmake --version

# 检查编译器
g++ --version  # 或 clang++ --version

# 检查 Google Test (如果手动安装)
pkg-config --modversion gtest
```

## 下一步

安装完成后，请按照 `运行测试说明.md` 中的说明运行测试。
