# 1. 声明 CMake 的最低版本要求
cmake_minimum_required(VERSION 3.10)

# 2. 项目名称
project(AsyncLogger)

# 3. 设置 C++ 标准 (既然要重构，直接上现代 C++17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 4. 指定头文件搜索路径 (告诉编译器 include 文件夹在哪)
include_directories(include)

# 5. 查找必要的库 (比如你一直在用的 pthread)
find_package(Threads REQUIRED)

# 6. 收集所有的源文件 (src 目录下的所有 .cpp)
file(GLOB SOURCES "src/*.cpp")

# 7. 生成可执行程序
add_executable(AsyncServer ${SOURCES})

# 8. 链接线程库 (非常重要，否则会报 pthread 相关未定义引用)
target_link_libraries(AsyncServer PRIVATE Threads::Threads)