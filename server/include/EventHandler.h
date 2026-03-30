#pragma once

#include <array>
#include <memory>
#include <unistd.h>
#include "ServerPub.h"
#include "RequestBuffer.h"

class EventHandler {
protected:
    int fd_;
public:
    EventHandler(int fd) : fd_(fd) {}
    virtual ~EventHandler();
    virtual EVENT_STATUS handle_event(unsigned int state) = 0;
};

class ClientHandler : public EventHandler {
public:
    static constexpr size_t BUFFER_SIZE = 2048;
    ClientHandler(int client_fd, std::shared_ptr<BufferPool<std::array<char, ClientHandler::BUFFER_SIZE>>> pool)
        : EventHandler(client_fd), buffer_pool_(pool) {}
    EVENT_STATUS handle_event(unsigned int state) override;
private:
    std::shared_ptr<BufferPool<std::array<char, BUFFER_SIZE>>> buffer_pool_;
};

class ListenHandler : public EventHandler {
    int epoll_fd_;
    std::shared_ptr<BufferPool<std::array<char, ClientHandler::BUFFER_SIZE>>> buffer_pool_;
public:
    ListenHandler(int listen_fd, int epoll, std::shared_ptr<BufferPool<std::array<char, ClientHandler::BUFFER_SIZE>>> pool) 
        : EventHandler(listen_fd), epoll_fd_(epoll), buffer_pool_(pool) {}
    ~ListenHandler();
    EVENT_STATUS handle_event(unsigned int state) override;
};