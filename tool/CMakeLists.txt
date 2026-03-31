# 1. 搜集 src 目录下所有的 .cpp 文件
file(GLOB TOOL_SOURCES "src/*.cpp")

# 2. 遍历每一个源文件
foreach(SOURCE_FILE ${TOOL_SOURCES})
    # 获取文件名（不带路径和后缀），作为可执行文件的名字
    # 例如：src/simple_nc.cpp -> simple_nc
    get_filename_component(BIN_NAME ${SOURCE_FILE} NAME_WE)

    # 3. 为每一个源文件创建一个独立的可执行程序
    add_executable(${BIN_NAME} ${SOURCE_FILE})

    # 4. 配置包含路径（让所有小工具都能用到 include 里的头文件）
    target_include_directories(${BIN_NAME} PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        # ${PROJECT_SOURCE_DIR}/server/include  # 如果需要用到主项目的头文件
    )

    # 5. 链接必要的库（如线程库、日志库等）
    target_link_libraries(${BIN_NAME} PRIVATE 
        Threads::Threads
        # asynclog_lib  # 如果你的工具也需要写日志
    )
    
    # 打印一下进度，方便编译时确认
    message(STATUS "Found tool source: ${BIN_NAME}, preparing to build...")
endforeach()