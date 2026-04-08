# 1. 声明 CMake 的最低版本要求
# [作用]：规定 CMake 的最低版本要求
# [原因]：确保所有开发者使用的构建工具版本一致，避免因版本差异导致编译失败
cmake_minimum_required(VERSION 3.10)

# [关键步骤]：在定义项目和子目录之前就开启导出功能
# 会生成一个 compile_commands.json，VS Code 读了它就能完美识别所有宏和包含路径
# 接着在.vscode目录下创建c_cpp_properties.json文件，把compile_commands.json加进去，即可解决头文件引用报错问题
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 2. 项目名称
# [作用]：定义项目名称
# [原因]：在生成 IDE 工程（如 VS 或 Xcode）时，这会成为解决方案的名称
project(CPPServer)

# 3. 设置 C++ 标准 (既然要重构，直接上现代 C++17)
# [作用]：强制要求使用 C++17 标准
# [原因]：异步日志用到了 std::atomic 和现代内存管理，C++17 提供了更好的性能和类型安全
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 加这一行：如果没有指定构建类型，默认用 Debug
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# 4. 配置ASAN及TSAN编译选项
# 4.1. 定义三个自定义开关，默认都是 OFF（关闭）
option(USE_ASAN "Enable AddressSanitizer for Memory Checking" OFF)          # [作用]：定义一个开关，控制是否启用 ASAN 内存检查
option(USE_TSAN "Enable ThreadSanitizer for Race/Deadlock Checking" OFF)    # [作用]：定义一个开关，控制是否启用 TSan 线程/死锁检查

# 4.2. 安全校验：ASAN 和 TSan 绝对不能同时开启！
if(USE_ASAN AND USE_TSAN)
    message(FATAL_ERROR "ASAN 和 TSan 不能同时开启！")
endif()

# 4.3. 如果开启了 ASAN
if(USE_ASAN)
    message(STATUS "🟢 ASAN (Memory Check) is ENABLED")
    # -fno-omit-frame-pointer 是为了让报错时的调用栈更清晰
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
endif()

# 4.4. 如果开启了 TSan (死锁与竞态检测)
if(USE_TSAN)
    message(STATUS "🔵 TSan (Thread/Deadlock Check) is ENABLED")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread -g")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
endif()

# 5. 查找必要的库 (比如你一直在用的 pthread)
# [作用]：在全系统范围内寻找线程库（pthread）
# [原因]：你的 ChatServer 和 LogFlusher 都严重依赖多线程，必须引入底层库支持
find_package(Threads REQUIRED)

# 核心：添加子目录
# [作用]：添加子目录模块
# [原因]：add_subdirectory 会触发子目录下的 CMakeLists.txt，实现“递归构建”
# 这样你只需要在根目录执行一次编译命令，所有模块都会自动按顺序编译
add_subdirectory(asynclog)
add_subdirectory(server)
add_subdirectory(tool)