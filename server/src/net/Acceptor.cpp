#include "Acceptor.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <future>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include "Channel.h"
#include "Logger.h"

Acceptor::~Acceptor()
{
    if (fd_ != -1) {
        close(fd_);
        fd_ = -1;
    }
}


void Acceptor::shutdown()
{
    if (fd_ == -1) {
        return;
    }
    channel_.unregister_channel();
    close(fd_);
    fd_ = -1;
    channel_.set_fd(fd_);
}

void Acceptor::stop()
{
    if (!loop_) {
        return;
    }
    // 通过promise和future机制，在线程安全的情况下等待Acceptor的shutdown完成，确保资源正确释放后再退出。
    std::promise<void> done;
    std::future<void> future_done = done.get_future();
    loop_->run_in_loop_thread([this, &done]() {
        this->shutdown();
        // 通知调用线程shutdown完成
        done.set_value();
    });
    // 等待shutdown完成，确保资源正确释放后再退出。
    future_done.wait(); 
    return;
}

int Acceptor::get_socket_fd(const char* ipaddr, const char* port)
{
    unsigned short port_num = static_cast<unsigned short>(std::stoi(port));
    auto sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        LOG_ERR("Socket creation failed, errorno[%d].", errno);
        return -1;
    }
    sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;   /* IPV4 */
    server_addr.sin_port = htons(port_num);
    auto ret = inet_pton(AF_INET, ipaddr, &server_addr.sin_addr.s_addr);
    if (ret <= 0) {
        LOG_ERR("Invalid server IP address, errorno[%d].", errno);
        return -1;
    }
    int opt = 1;
    /* 设置端口复用，允许服务器重启后立即绑定同一端口，避免TIME_WAIT状态导致的绑定失败问题。 */
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ret = bind(sockfd, static_cast<sockaddr*>(static_cast<void*>(&server_addr)), sizeof(sockaddr_in));
    if (ret == -1) {
        LOG_ERR("Socket binding failed, errorno[%d]. erroreason %s", errno, strerror(errno));
        return -1;
    }
    return sockfd;
}

void Acceptor::init_socket_fd(const char* ipaddr, const char* ipport)
{
    LOG_INFO("Acceptor binding %s:%s begin.", ipaddr, ipport);
    fd_ = get_socket_fd(ipaddr, ipport);
    if (fd_ == -1) {
        return;
    }
    auto ret = listen(fd_ , ACCEPTOR_SPEC::BACKBLOG_LEN);
    if (ret == -1) {
        LOG_ERR("Socket listening failed, errorno[%d].", errno);
        return;
    }
    // 坑一：监听套接字必须设置为非阻塞，否则在accept时可能会被阻塞，占用线程，导致整个服务器无法响应其他事件。
    ret = fcntl(fd_ , F_SETFL, O_NONBLOCK);
    if (ret == -1) {
        LOG_ERR("sockfd set O_NONBLOCK mode err!, errorno[%d].", errno);
    }
}

void Acceptor::attach_socket(const char* ipaddr, const char* ipport)
{
    init_socket_fd(ipaddr, ipport);
    if (fd_  == -1) {
        exit(EXIT_FAILURE);
    }
    LOG_INFO("Acceptor fd[%d] listening TCP %s:%s", fd_, ipaddr, ipport);
    return;
}

void Acceptor::set_channel_dispatch_callback(std::function<void(const int&)> callback)
{
    channel_dispatch_callback = std::move(callback);
}

void Acceptor::accept_connection()
{
    struct sockaddr addr;
    socklen_t len = sizeof(sockaddr);
    while (true) {
        auto client_fd = accept(fd_ , &addr, &len);
        if (client_fd == -1) {
            if (errno == EINTR) {
                LOG_INFO("System interrupt Acceptor fd[%d] to get client fd. errono[%d]", fd_ , errno);
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                LOG_INFO("Acceptor fd[%d] accept client over.", fd_ );
                break;
            }
            LOG_INFO("Acceptor fd[%d] occur readding failed. errorno[%d]", fd_ , errno);
            return;
        }
        sockaddr_in* client_addr = reinterpret_cast<sockaddr_in*>(&addr);
        LOG_INFO("Acceptor get client fd:%d [%s:%u] connection.", client_fd, inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port));
        channel_dispatch_callback(client_fd);
    }
    return;
}

void Acceptor::attach_loop(EventLoop* loop)
{
    loop_ = loop;
    channel_.set_fd(fd_);
    channel_.set_read_callback([this]() {
        return accept_connection();
    });
    channel_.enable_reading_notify();
    channel_.attatch_to_loop(loop_);
    channel_.register_channel();
}
