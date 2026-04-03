#!/bin/bash
# 文件名: server_build_and_excute.sh
# 描述: 编译并执行服务器程序
# 使用方法:
#   ./server_build_and_excute.sh            # 默认编译并执行服务器
#   ./server_build_and_excute.sh -s         # 使用 ASAN 内存检查模式编译并
#   ./server_build_and_excute.sh -t         # 使用 TSan 线程检查模式编译并执行

# 默认参数
CMAKE_ARGS=""

# 解析传入的参数
while getopts "st" opt; do
  case $opt in
    s)
      echo "开启 ASAN 内存检查模式..."
      CMAKE_ARGS="-DUSE_ASAN=ON"
      ;;
    t)
      echo "开启 TSan 线程检查模式..."
      CMAKE_ARGS="-DUSE_TSAN=ON"
      ;;
    \?)
      echo "无效参数"
      exit 1
      ;;
  esac
done

# 执行编译
mkdir -p build && cd build
rm -rf *
cmake $CMAKE_ARGS ..
make -j
echo "编译完成！"
# 执行服务器
echo "正在启动服务器..."
./server/WebServer 192.168.182.128 8888 # 请根据实际情况修改服务器 IP 地址和端口号