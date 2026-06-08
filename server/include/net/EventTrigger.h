#pragma once

#include <functional>
#include <mutex>
#include "Channel.h"

namespace EVENTTRIGGER_SPEC {
    constexpr unsigned short TASK_QUEUE_INIT_SIZE = 1024;
}

class EventLoop;

class EventTrigger {
    int event_fd_;
    // loop_在此意味该类所归属线程
    EventLoop* loop_;
    std::mutex task_queue_mutex_;
    std::vector<std::function<void()>> task_queue_;
    Channel channel_;

    void stop();
public:
    EventTrigger(EventLoop*);
    ~EventTrigger();

    void add_task(std::function<void()>);
    void wake_up();
    void handle_wake_up();
    void do_pending_tasks();
    void register_to_loop();
    void close_trigger();
};