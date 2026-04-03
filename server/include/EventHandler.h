#pragma once

#include <array>
#include <memory>
#include <unistd.h>
#include <vector>
#include "ServerPub.h"
#include "RequestBuffer.h"

class Reactor;
class EventHandler {
protected:
    int fd_;
public:
    EventHandler(int fd) : fd_(fd) {}
    virtual ~EventHandler();
    virtual EVENT_STATUS handle_event(unsigned int state) = 0;
};

/*
    socket_fd监听器处理类
*/
class ClientHandler : public EventHandler {
public:
    ClientHandler(const int&,
        std::shared_ptr<BufferPool<std::array<char, SPECS_VALUE::FD_READ_SIZE>>>&,
        std::shared_ptr<Reactor>&);
    EVENT_STATUS handle_event(unsigned int state) override;
private:
    std::shared_ptr<BufferPool<std::array<char, SPECS_VALUE::FD_READ_SIZE>>> buffer_pool_;
    std::weak_ptr<Reactor> reactor_; // 客户端存放的是当前所受管理的Reactor
    // 因Reactor的Connection表中存放的EventHandler是ClientHandler，所以此处为避免循环引用，使用弱指针
};

class ListenHandler : public EventHandler {
    int epoll_fd_;
    std::shared_ptr<BufferPool<std::array<char, SPECS_VALUE::FD_READ_SIZE>>> buffer_pool_;
    std::shared_ptr<std::vector<std::shared_ptr<Reactor>>> reactors_; // 服务端村粗轮询备Reactor表
    unsigned int robin_count_; // 轮询计数
public:
    ListenHandler(const int&, int&,
        std::shared_ptr<BufferPool<std::array<char, SPECS_VALUE::FD_READ_SIZE>>>&,
        std::shared_ptr<std::vector<std::shared_ptr<Reactor>>>&);
    EVENT_STATUS handle_event(unsigned int state) override;
};