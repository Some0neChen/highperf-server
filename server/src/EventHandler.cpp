#include "EventHandler.h"
#include "Logger.h"
#include "ServerPub.h"
#include <cerrno>
#include <cstring>
#include <memory>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include "TaskPacket.h"

#include "ConnectionManager.h"

EventHandler::~EventHandler() {
    close(this->fd_);
}

ListenHandler::~ListenHandler() {
    close(this->epoll_fd_);
}

EVENT_STATUS ListenHandler::handle_event(unsigned int state) {
    struct sockaddr addr;
    socklen_t len = sizeof(sockaddr);
    int client_fd = accept(this->fd_, &addr, &len);

    if (client_fd == -1) {
        LOG_ERR("get client fd err.");
        return EVENT_STATUS::ACCEPT_FD_ERR;
    }
    sockaddr_in* client_addr = reinterpret_cast<sockaddr_in*>(&addr);
    LOG_INFO("get client fd:%d [%u:%u] connection.", client_fd, ntohl(client_addr->sin_addr.s_addr), ntohs(client_addr->sin_port));

    auto listen_state = EPOLLIN | EPOLLET | EPOLLRDHUP;
    auto ret = ConnectionManager::get_manager().add_connection(client_fd, listen_state, std::make_shared<ClientHandler>(client_fd, this->buffer_pool_));
    if (ret != EVENT_STATUS::OK) {
        LOG_ERR("epoll add client_fd err.");
        
    }
    return ret;;
}

EVENT_STATUS ClientHandler::handle_event(unsigned int state) {
    LOG_INFO("Client fd[%d] readding begain.", this->fd_);
    if (state & EPOLLRDHUP) {
        LOG_INFO("Client fd[%d] closed connection.", this->fd_);
        ConnectionManager::get_manager().reset_connection(this->fd_);
        return EVENT_STATUS::OK;
    }
    if (!(state & EPOLLIN)) {
        LOG_ERR("Undefined epoll behavior. fd[%d].", this->fd_);
        return EVENT_STATUS::EPOLL_UNDEFINE_TRIG;
    }
    while (true) {
        auto buf = this->buffer_pool_->get_empty_buffer();
        size_t len = read(this->fd_, buf->data(), buf->size());
        if (len == -1) {
            if (errno == EINTR) {
                LOG_INFO("System interrupt client fd[%d] reading. errono[%d]", this->fd_, errno);
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                LOG_INFO("Client fd[%d] readding over.", this->fd_);
                break;;
            }
            LOG_INFO("Client fd[%d] occur readding failed. errorno[%d]", this->fd_, errno);
            return EVENT_STATUS::CLIENT_READ_ERR;
        }
        TaskManager::get_manager().add_task(buf, this->fd_, len);
    }
    
    return EVENT_STATUS::OK;
}