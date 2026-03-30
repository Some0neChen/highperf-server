# [作用]：自动收集 src 目录下所有的 .cpp 源文件
# [原因]：避免手动输入每一个文件名。当你以后增加新的日志功能文件时，CMake 会自动识别
file(GLOB LOG_SOURCES "src/*.cpp")
#  排除掉测试用的那个 cpp
#  此后生成的库就不包含main函数
list(REMOVE_ITEM LOG_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/LogTest.cpp")

# [作用]：将日志代码编译成静态库 (asynclog_lib)
# [原因]：日志是一个独立的功能组件，编译成库可以提高复用性，且 WebServer 编译时只需链接它即可
add_library(asynclog_lib STATIC ${LOG_SOURCES})

# [作用]：指定头文件路径，并设为 PUBLIC
# [原因]：PUBLIC 意味着：1. 编译本库时要用 include；
#                       2. 谁链接了这个库（比如 WebServer），谁也会自动获得这个 include 路径
# 这样你以后在 server 模块里 `#include "Logger.h"` 就不用写长长的相对路径了
target_include_directories(asynclog_lib PUBLIC include)

# [作用]：给日志库链接线程支持
# [原因]：LogFlusher 内部开启了 std::thread 来进行异步落盘，需要链接内核线程库
target_link_libraries(asynclog_lib PRIVATE Threads::Threads)


# 生成日志压测可执行程序
# [作用]：定义一个名为 LogStressTest 的程序，通过LogTest.cpp里的main函数生产压测程序
# [源文件]：只包含那个带 main 函数的文件
add_executable(LogStressTest src/LogTest.cpp)
target_link_libraries(LogStressTest PRIVATE asynclog_lib Threads::Threads)