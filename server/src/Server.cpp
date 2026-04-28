#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <memory>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include "Logger.h"
#include "ServerPub.h"
#include "ThreadPool.h"
#include "HttpModule.h"

using namespace std;

atomic<bool> server_running;

void signal_handler(int sig) {
    server_running.store(false);
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        LOG_ERR("Input pram error! Please input <server_ip> <server_port>");
        exit(EXIT_FAILURE);
    }
    // 获取TCP服务端Sokcet_fd
    auto socket_fd = init_socket_fd(argc, argv);

    // 加载守护线程及看管模块
    server_running.store(true);
    signal(SIGINT, signal_handler);
    signal(SIGRTMIN, signal_handler);
    // if (daemon(0, 0) == -1) {
    //     LOG_ERR("daemon err! errorno %d.", errno);
    //     exit(EXIT_FAILURE);
    // }

    // 线程池、缓冲池、任务池初始化
    shared_ptr<ThreadPool<function<EVENT_STATUS()>>> threadPool =
        make_shared<ThreadPool<function<EVENT_STATUS()>>>(16);

    // TCP层主副Reactor初始化及挂接
    auto ret = tcp_server_main_reactor_register(socket_fd, threadPool);

    // Http解析状态机及响应报文构造器挂接
    init_http_parse_fsm();
    HttpRouteAttach();

    // 维持主线程不挂
    while (server_running.load()) {
        usleep(100000);
    }

    exit(EXIT_SUCCESS);
}