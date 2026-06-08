# [作用]：收集 Test 目录下的源文件
# [注意]：不要多次 file(GLOB 同一个变量)，后一次会覆盖前一次。
file(GLOB SERVER_APP_SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.cpp")
file(GLOB SERVER_BASE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/base/*.cpp")
file(GLOB SERVER_NET_SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/net/*.cpp")
file(GLOB SERVER_HTTP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/http/*.cpp")

set(SERVER_SOURCES
    ${SERVER_APP_SOURCES}
    ${SERVER_BASE_SOURCES}
    ${SERVER_NET_SOURCES}
    ${SERVER_HTTP_SOURCES}
)

if(NOT SERVER_SOURCES)
    message(FATAL_ERROR "No source files found for SERVER_SOURCES")
endif()

# [作用]：生成最终的可执行程序 (WebServer)
# [原因]：这是你项目的最终产物，也就是面试官运行你看效果的地方
add_executable(WebServer ${SERVER_SOURCES})

# [作用]：包含本模块的头文件路径
# [原因]：WebServer 自己的业务逻辑头文件（如 HttpHandle.h）存放在这里
# [作用]：包含 Test 模块自己的头文件路径
target_include_directories(WebServer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/base
    ${CMAKE_CURRENT_SOURCE_DIR}/include/net
    ${CMAKE_CURRENT_SOURCE_DIR}/include/http
)

# [作用]：链接异步日志库
# [原因]：这是现代 CMake 的精髓！链接 asynclog_lib 后，它会自动把日志库的二进制代码、
# 以及刚才设置成 PUBLIC 的 include 目录全部“继承”给 WebServer
target_link_libraries(WebServer PRIVATE asynclog_lib)
target_link_libraries(WebServer PRIVATE Threads::Threads)