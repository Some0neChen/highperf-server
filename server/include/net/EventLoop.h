#pragma once

#include "Channel.h"
#include "EventTrigger.h"
#include <atomic>
#include <sys/epoll.h>
#include <thread>
#include <vector>

namespace EVENTLOOP_SPEC {
    constexpr unsigned short EVENT_INIT_NUM = 1024;
}

class EventTrigger;

class EventLoop {
    // Loop线程
    std::thread loop_thread_;
    std::atomic<bool> quit_;

    int epoll_fd_;
    std::vector<epoll_event> events_;
    EventTrigger event_trigger_;
    
    bool is_in_loop_thread();
public:
    EventLoop();
    ~EventLoop();
    
    void loop();
    void start();
    void run_in_loop_thread(std::function<void()>);
    void queue_in_loop(std::function<void()>);

    void register_channel(Channel*);
    void update_channel(Channel*);
    void unregister_channel(Channel*);
};