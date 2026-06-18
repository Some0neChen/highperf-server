#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <unistd.h>
#include "HttpServer.h"
#include "Logger.h"
#include "TCPConnection.h"
#include "TCPServer.h"
#include "HttpPub.h"

using namespace std;

atomic<bool> server_running;

void signal_handler(int sig) {
    server_running.store(false);
}

int main(int argc, char** argv)
{
    cout << "Hello WebServer!" << endl;
    if (argc != 3) {
        LOG_ERR("Input pram error! Please input <server_ip> <server_port>");
        exit(EXIT_FAILURE);
    }
    // 注册退出信号
    signal(SIGINT, signal_handler);
    signal(SIGRTMIN, signal_handler);
    // 忽略SIGPIPE信号，防止写管道时对方关闭导致进程被杀死
    signal(SIGPIPE, SIG_IGN);

    Http_module_init();
    HttpServer httpServer;

    TCPServer tcpServer;
    // 4次级Reactor(IO线程)，开启TCPConnection活跃定时器
    tcpServer.spec_set(2, true);
    tcpServer.set_upper_sync_create_callback([&httpServer](std::shared_ptr<TCPConnection> tcp_conn) {
        httpServer.set_http_context(tcp_conn);
    });
    tcpServer.set_parse_ready_callback([&httpServer](std::weak_ptr<TCPConnection> conn) {
        httpServer.read_tcp_buffer(conn);
    });
    tcpServer.start(argv[1], argv[2]);

    server_running.store(true);

    while (server_running.load()) {
        usleep(100000);
    }

    return EXIT_SUCCESS;
}