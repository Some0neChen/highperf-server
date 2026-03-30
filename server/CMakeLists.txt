# [作用]：收集 server 目录下的源文件
file(GLOB SERVER_SOURCES "src/*.cpp")
list(REMOVE_ITEM SERVER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/ServerTest.cpp")

# [作用]：生成最终的可执行程序 (WebServer)
# [原因]：这是你项目的最终产物，也就是面试官运行你看效果的地方
add_executable(WebServer ${SERVER_SOURCES})

# [作用]：包含本模块的头文件路径
# [原因]：WebServer 自己的业务逻辑头文件（如 HttpHandle.h）存放在这里
target_include_directories(WebServer PRIVATE include)

# [作用]：链接异步日志库
# [原因]：这是现代 CMake 的精髓！链接 asynclog_lib 后，它会自动把日志库的二进制代码、
# 以及刚才设置成 PUBLIC 的 include 目录全部“继承”给 WebServer
target_link_libraries(WebServer PRIVATE asynclog_lib)
target_link_libraries(WebServer PRIVATE Threads::Threads)


add_executable(WebServerTest src/ServerTest.cpp)
target_include_directories(WebServerTest PRIVATE include)
target_link_libraries(WebServerTest PRIVATE asynclog_lib)
target_link_libraries(WebServerTest PRIVATE Threads::Threads)