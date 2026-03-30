#include <arpa/inet.h>
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
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include "Logger.h"
#include "RequestBuffer.h"
#include "ServerPub.h"
#include "ThreadPool.h"
#include "EventHandler.h"
#include "ConnectionManager.h"
#include "TaskPacket.h"

using namespace std;

atomic<bool> server_running;

void signal_handler(int sig) {
    server_running.store(false);
}

int get_socket_fd(const char* ip, const unsigned short* port) {
    auto sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        LOG_ERR("Socket creation failed");
        return -1;
    }
    sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(*port);
    auto ret = inet_pton(AF_INET, ip, &server_addr.sin_addr.s_addr);
    if (ret <= 0) {
        LOG_ERR("Invalid server IP address");
        return -1;
    }
    ret = bind(sockfd, static_cast<sockaddr*>(static_cast<void*>(&server_addr)), sizeof(sockaddr_in));
    if (ret == -1) {
        LOG_ERR("Socket binding failed");
        return -1;
    }
    return sockfd;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        LOG_ERR("Input pram error! Please input <server_ip> <server_port>");
        exit(EXIT_FAILURE);
    }

    server_running.store(true);
    signal(SIGINT, signal_handler);
    signal(SIGRTMIN, signal_handler);
    // if (daemon(0, 0) == -1) {
    //     LOG_ERR("daemon err! errorno %d.", errno);
    //     exit(EXIT_FAILURE);
    // }


    unsigned short port_num = static_cast<unsigned short>(std::stoi(argv[2]));
    auto sockfd = get_socket_fd(argv[1], &port_num);
    if (sockfd == -1) {
        exit(EXIT_FAILURE);
    }
    auto ret = listen(sockfd, 32);
    if (ret == -1) {
        LOG_ERR("Socket listening failed");
        exit(EXIT_FAILURE);
    }

    shared_ptr<ThreadPool<function<EVENT_STATUS()>>> threadPool =
        make_shared<ThreadPool<function<EVENT_STATUS()>>>();
    auto buffer_pool = make_shared<BufferPool<std::array<char, ClientHandler::BUFFER_SIZE>>>();
    TaskManager::get_manager().register_thread_pool(threadPool);

    int epoll_fd = epoll_create(1);
    if (epoll_fd == -1) {
        LOG_ERR("epoll create err.");
        exit(EXIT_FAILURE);
    }
    ConnectionManager::get_manager().register_epoll_fd(epoll_fd);
    ret = ConnectionManager::get_manager().add_connection(
        sockfd, EPOLLIN, make_shared<ListenHandler>(sockfd, epoll_fd, buffer_pool));
    if (ret != EVENT_STATUS::OK) {
        LOG_ERR("epoll add listen fd err. ret %d.", ret);
        exit(EXIT_FAILURE);
    }

    constexpr size_t MAX_EVENTS = 1024;
    epoll_event events[MAX_EVENTS];

    while (server_running.load()) {
        ret = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (ret == -1) {
            if (errno == EINTR) {
                // 这只是被调试器或信号打断了，不是真错误，继续下一次循环即可
                continue; 
            }
            LOG_ERR("epoll wait err. get retval %d.", ret);
            exit(EXIT_FAILURE);
        }
        LOG_INFO("get epoll wait ret %d", ret);
        for (int i = 0; i < ret; ++i) {
            LOG_INFO("get trigger fd %d", events[i].data.fd);
            threadPool->add_task([&events, i]() {
                if (ConnectionManager::get_manager()[events[i].data.fd] == nullptr) {
                    return EVENT_STATUS::EPOLL_EVENT_MISS;
                }
                return ConnectionManager::get_manager()[events[i].data.fd]->handle_event(events[i].events);
            });
        }
    }
    close(sockfd);
    exit(EXIT_SUCCESS);
}