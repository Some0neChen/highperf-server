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
mkdir -p ../build && cd ../build
rm -rf *
cmake $CMAKE_ARGS ..
make -j
echo "编译完成！"
# 执行服务器
echo "正在启动服务器..."

# setarch -R 的作用就是告诉 Linux 内核：“运行这个程序时，不要做地址随机化。”地址随机化是一种安全特性，它会在每次程序运行时随机化内存地址，以防止攻击者利用固定的内存地址进行攻击。使用 setarch -R 可以确保服务器程序在每次运行时使用相同的内存地址，这对于调试和分析非常有帮助。
setarch $(uname -m) -R ./server/WebServer 192.168.182.128 8888 # 请根据实际情况修改服务器 IP 地址和端口号