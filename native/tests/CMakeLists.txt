cmake_minimum_required(VERSION 3.10)
project(fastbot_tests)

# 设置C++标准
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找Google Test
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    # 如果找不到，尝试使用FetchContent下载
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
endif()

# 包含目录
include_directories(
    "../"
    "../agent/"
    "../desc/"
    "../desc/reuse/"
    "../model/"
    "../events/"
    "../thirdpart/"
    "../thirdpart/flatbuffers/"
    "../thirdpart/tinyxml2/"
    "../thirdpart/json/"
)

# 源文件列表（排除JNI和main）
file(GLOB_RECURSE SRC_FILES
    "../*.cpp"
    "../agent/*.cpp"
    "../desc/*.cpp"
    "../desc/reuse/*.cpp"
    "../model/*.cpp"
    "../events/*.cpp"
    "../thirdpart/tinyxml2/*.cpp"
    "../thirdpart/flatbuffers/*.cpp"
)

# 排除JNI文件
list(FILTER SRC_FILES EXCLUDE REGEX ".*fastbot_native\\.cpp$")
list(FILTER SRC_FILES EXCLUDE REGEX ".*project/jni.*")

# 测试源文件（排除README等）
file(GLOB TEST_SOURCES 
    "test_*.cpp"
)

# 创建测试可执行文件
add_executable(fastbot_tests ${TEST_SOURCES} ${SRC_FILES})

# 链接库
target_link_libraries(
    fastbot_tests
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
    pthread
)

# 启用测试
enable_testing()

# 添加测试
add_test(NAME FastbotTests COMMAND fastbot_tests)

# 设置编译选项
target_compile_options(fastbot_tests PRIVATE
    -Wall
    -Wextra
    -Werror
    -Wno-error=deprecated-declarations
    -g
    -O0
    -fprofile-arcs
    -ftest-coverage
)

# 链接时添加覆盖率选项
target_link_options(fastbot_tests PRIVATE
    --coverage
)
