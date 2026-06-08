#pragma once
#include "Channel.h"
#include "EventLoop.h"
#include <functional>

namespace ACCEPTOR_SPEC {
    constexpr unsigned short BACKBLOG_LEN = 128;
}

class Channel;
class Acceptor {
    int fd_;
    // loop_在此意味该类所归属线程
    EventLoop* loop_;
    Channel channel_;
    std::function<void(const int&)> channel_dispatch_callback;

    int get_socket_fd(const char* ipaddr, const char* port);
    void init_socket_fd(const char* ipaddr, const char* ipport);

    void shutdown();
public:
    Acceptor() = default;
    ~Acceptor();
    void attach_socket(const char* ipaddr, const char* ipport);
    void set_channel_dispatch_callback(std::function<void(const int&)>);
    void accept_connection();
    void attach_loop(EventLoop*);
    void stop();
};