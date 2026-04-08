#include "EventHandler.h"
#include "Logger.h"
#include "ServerPub.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include "TaskPacket.h"
#include "Reactor.h"

EventHandler::~EventHandler() {
    close(this->fd_);
}

ListenHandler::ListenHandler(const int& listen_fd, int& epoll,
    std::shared_ptr<BufferPool<std::array<char, SPECS_VALUE::FD_READ_SIZE>>>& pool,
    std::shared_ptr<std::vector<std::shared_ptr<Reactor>>>& reactors) :
    EventHandler(listen_fd), epoll_fd_(epoll), buffer_pool_(pool), reactors_(reactors), robin_count_(0)
{
    if (fcntl(this->fd_, F_SETFL, O_NONBLOCK) < 0) {
        LOG_ERR("Server fd[%d] set O_NONBLOCK mode err.", this->fd_);
        exit(EXIT_FAILURE);
    }
}

EVENT_STATUS ListenHandler::handle_event(unsigned int state)
{
    struct sockaddr addr;
    socklen_t len = sizeof(sockaddr);

    if (!(state & EPOLLIN)) {
        LOG_ERR("Undefined epoll behavior. fd[%d].", this->fd_);
        return EVENT_STATUS::EPOLL_UNDEFINE_TRIG;
    }
    while (true) {
        auto client_fd = accept(this->fd_, &addr, &len);
        if (client_fd == -1) {
            if (errno == EINTR) {
                LOG_INFO("System interrupt listen fd[%d] to get client fd. errono[%d]", this->fd_, errno);
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                LOG_INFO("Listen fd[%d] accept client over.", this->fd_);
                break;;
            }
            LOG_INFO("Listen fd[%d] occur readding failed. errorno[%d]", this->fd_, errno);
            return EVENT_STATUS::CLIENT_READ_ERR;
        }
        sockaddr_in* client_addr = reinterpret_cast<sockaddr_in*>(&addr);
        LOG_INFO("get client fd:%d [%s:%u] connection.", client_fd, inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port));
        auto listen_state = EPOLLIN | EPOLLET | EPOLLRDHUP;
        ++this->robin_count_;
        auto reactor = this->reactors_->at(robin_count_ % this->reactors_->size());
        auto ret = reactor->
            add_connection(client_fd, listen_state, std::make_shared<ClientHandler>(client_fd, this->buffer_pool_, reactor));
        if (ret != EVENT_STATUS::OK) {
            LOG_ERR("epoll add client_fd err.");
            return ret;  
        }
    }
    return EVENT_STATUS::OK;;
}

ClientHandler::ClientHandler(const int& fd,
        std::shared_ptr<BufferPool<std::array<char, SPECS_VALUE::FD_READ_SIZE>>>& buf_pool,
        std::shared_ptr<Reactor>& reactor) : EventHandler(fd), buffer_pool_(buf_pool), reactor_(reactor)
{
    if (fcntl(this->fd_, F_SETFL, O_NONBLOCK) < 0) {
        LOG_ERR("Client fd[%d] set O_NONBLOCK mode err.", this->fd_);
    }
}

EVENT_STATUS ClientHandler::handle_event(unsigned int state)
{
    LOG_INFO("Client fd[%d] readding begain.", this->fd_);
    if (this->reactor_.lock() == nullptr) {
        LOG_ERR("Client fd[%d] get reactor failed. The reactor is already released.", this->fd_);
        return EVENT_STATUS::CLIENT_UNBIND_REACTOR_ERR;
    }
    if (state & EPOLLRDHUP) {
        LOG_INFO("Client fd[%d] closed connection.", this->fd_);
        this->reactor_.lock()->reset_connection(this->fd_);
        return EVENT_STATUS::OK;
    }
    if (!(state & EPOLLIN)) {
        LOG_ERR("Undefined epoll behavior. fd[%d].", this->fd_);
        return EVENT_STATUS::EPOLL_UNDEFINE_TRIG;
    }
    while (true) {
        auto buf = this->buffer_pool_->get_empty_buffer();
        ssize_t len = read(this->fd_, buf->data(), buf->size()); // len不能用size_t，因为size_t本质是无符号数，没有-1
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