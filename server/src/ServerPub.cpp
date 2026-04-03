/*
    TCP层提取函数
*/

#include "ServerPub.h"
#include "EventHandler.h"
#include "Logger.h"
#include "TaskPacket.h"
#include <arpa/inet.h>
#include <cerrno>
#include <sys/epoll.h>
#include <unistd.h>
#include <functional>
#include <memory>
#include <vector>
#include "Reactor.h"

void http_parse() {
    // TODO
}

int get_socket_fd(const char* ip, const unsigned short* port) {
    auto sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        LOG_ERR("Socket creation failed, errorno[%d]", errno);
        return -1;
    }
    sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(*port);
    auto ret = inet_pton(AF_INET, ip, &server_addr.sin_addr.s_addr);
    if (ret <= 0) {
        LOG_ERR("Invalid server IP address, errorno[%d]", errno);
        return -1;
    }
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ret = bind(sockfd, static_cast<sockaddr*>(static_cast<void*>(&server_addr)), sizeof(sockaddr_in));
    if (ret == -1) {
        LOG_ERR("Socket binding failed, errorno[%d]", errno);
        return -1;
    }
    return sockfd;
}

int init_socket_fd(int argc, char* argv[]) {
    unsigned short port_num = static_cast<unsigned short>(std::stoi(argv[2]));
    auto sockfd = get_socket_fd(argv[1], &port_num);
    if (sockfd == -1) {
        return sockfd;
    }
    auto ret = listen(sockfd, 32);
    if (ret == -1) {
        LOG_ERR("Socket listening failed, errorno[%d]", errno);
        return sockfd;
    }
    // 坑一：监听套接字必须设置为非阻塞，否则在accept时可能会被阻塞，占用线程，导致整个服务器无法响应其他事件。
    ret = fcntl(sockfd, F_SETFL, O_NONBLOCK);
    if (ret == -1) {
        LOG_ERR("sockfd set O_NONBLOCK mode err!, errorno[%d]", errno);
        return -1;
    }
    return sockfd;
}

std::shared_ptr<Reactor> tcp_server_main_reactor_register(const int& socket_fd,
    std::shared_ptr<ThreadPool<std::function<EVENT_STATUS()>>>& threadPool,
    std::shared_ptr<BufferPool<std::array<char, SPECS_VALUE::FD_READ_SIZE>>>& buffer_pool)
{
    // 主Reactor初始化
    std::shared_ptr<Reactor> mainReacotr = std::make_shared<Reactor>(threadPool);

    // 轮询副reactor初始化
    auto reactor_pool = std::make_shared<std::vector<std::shared_ptr<Reactor>>>();
    reactor_pool->reserve(3);
    for (int i = 0; i < reactor_pool->size(); ++i) {
        reactor_pool->push_back(std::make_shared<Reactor>(threadPool));
    }
    auto epoll_fd = mainReacotr->get_epoll_fd();

    // 客户端连接接收处理器
    std::shared_ptr<ListenHandler> listen_handler
        = std::make_shared<ListenHandler>(socket_fd, epoll_fd, buffer_pool, reactor_pool);
    auto ret = mainReacotr->add_connection(socket_fd, EPOLLIN | EPOLLET, listen_handler);

    if (ret != EVENT_STATUS::OK) {
        return nullptr;
    }
    return mainReacotr;
}

EVENT_STATUS task_handle(std::shared_ptr<TaskPacket> task)
{
    task->buffer->at(task->len) = '\0';
    LOG_INFO("Client fd[%d] handle msg[%s] beginning", task->fd, task->buffer->data());
    http_parse(); // 假装有http解析器
    std::string reply("Recive OK. Msg: ");
    reply.append(task->buffer->data());
    int ret = write(task->fd, reply.data(), reply.size());
    if (ret <= 0) {
        LOG_ERR("Client fd[%d] reply err. errorno[%d].", task->fd, errno);
        return  EVENT_STATUS::CLIENT_SEND_ERR;
    }
    LOG_INFO("Client fd[%d] reply over. Expect replied len %d. Actually replied len %d.",
        task->fd, reply.size(), ret);
    return EVENT_STATUS::OK;
}