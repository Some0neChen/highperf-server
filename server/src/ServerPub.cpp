/*
    TCP层提取函数
*/

#include "ServerPub.h"
#include "EventHandler.h"
#include "Logger.h"
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
    std::shared_ptr<ThreadPool<std::function<EVENT_STATUS()>>>& threadPool)
{
    // 主Reactor初始化
    std::shared_ptr<Reactor> mainReacotr = std::make_shared<Reactor>(threadPool);

    // 轮询副reactor初始化
    auto reactor_pool = std::make_shared<std::vector<std::shared_ptr<Reactor>>>();
    reactor_pool->reserve(3);
    for (int i = 0; i < reactor_pool->capacity(); ++i) {
        reactor_pool->push_back(std::make_shared<Reactor>(threadPool));
    }
    auto epoll_fd = mainReacotr->get_epoll_fd();

    // 客户端连接接收处理器
    std::shared_ptr<ListenHandler> listen_handler
        = std::make_shared<ListenHandler>(socket_fd, epoll_fd, reactor_pool);
    auto ret = mainReacotr->add_connection(socket_fd, EPOLLIN | EPOLLET, listen_handler);

    if (ret != EVENT_STATUS::OK) {
        return nullptr;
    }
    return mainReacotr;
}

EVENT_STATUS ClientHandler::task_handle(std::shared_ptr<TaskPacket> task)
{
    LOG_INFO("[%s]task_handle fd[%d] begin", __func__, task->fd);

    LOG_INFO("-----------------------------------------------------------"
             "ClientHandler[%d] parse http requst info:\r\n"
             "Method: %s\r\n"
             "URL: %s\r\n"
             "Version: %s\r\n"
             "Keep-Alive: %s\r\n"
             "Content-Length: %u\r\n",
             task->fd, task->request_header_->method.c_str(),
             task->request_header_->url.c_str(),
             task->request_header_->version.c_str(),
             task->request_header_->keep_alive ? "true" : "false",
             task->request_header_->content_length);
    LOG_INFO("Header:");
    for (auto it = task->request_header_->headers.begin(); it != task->request_header_->headers.end(); ++it) {
        LOG_INFO("%s: %s", it->first.c_str(), it->second.c_str());
    }
    LOG_INFO("Body: \r\n"
             "%s\r\n"
             "-----------------------------------------------------------", task->request_header_->body.c_str());
    
    // 构造404报文
    std::string body = "<html><body><h1>404 Not Found</h1><p>The requested resource was not found on this server.</p></body></html>";
    // 注意：每一行都要以 \r\n 结尾，Header 和 Body 之间有两个 \r\n
    std::string response = "HTTP/1.1 404 Not Found\r\n";
    response += "Content-Type: text/html; charset=utf-8\r\n";
    response += "Content-Length: " + std::to_string(body.length()) + "\r\n";
    response += "Connection: close\r\n";  // 告诉客户端发完就断开，适合简单测试
    response += "\r\n";                   // 关键的空行
    response += body;                     // 放入 Body

    // 3. 发送数据
    send(task->fd, response.c_str(), response.length(), MSG_NOSIGNAL);
    // 4. 非Keep-Alive以及非HTTP/1.1用完即关
    if (!task->request_header_->keep_alive || task->request_header_->version != "HTTP/1.1") {
        this->reactor_.lock()->reset_connection(task->fd);
    }
    
    return EVENT_STATUS::OK;
}