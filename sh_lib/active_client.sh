#!/bin/bash
# 文件名: active_client.sh
# 描述: 编译并执行服务器程序
# 使用方法:
#   ./active_client.sh          # 启动客户端，连接到下面设定的地址和端口
SERVER_ADDR="192.168.182.128"
SERVER_PORT=8888


# 执行连接
echo "正在启动客户端..."

# setarch -R 的作用就是告诉 Linux 内核：“运行这个程序时，不要做地址随机化。”地址随机化是一种安全特性，它会在每次程序运行时随机化内存地址，以防止攻击者利用固定的内存地址进行攻击。使用 setarch -R 可以确保服务器程序在每次运行时使用相同的内存地址，这对于调试和分析非常有帮助。
setarch $(uname -m) -R ../build/tool/TcpClient ${SERVER_ADDR} ${SERVER_PORT}