#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <queue>
#include <utility>
#include <vector>
#include "Channel.h"

namespace TIMER_SPEC {
    const unsigned int SERVER_INTERVAL = 20 * 60; // webserver定时器唤醒间隔，秒级
}

struct TimerPacket {
    std::chrono::steady_clock::time_point trig_time;
    std::function<void()> trigger;
    TimerPacket(std::chrono::steady_clock::time_point ddl, std::function<void()> task)
        : trig_time(std::move(ddl)), trigger(std::move(task)) {}
};

struct TimerPacketComparator {
    bool operator()(const TimerPacket& lhs, const TimerPacket& rhs) {
        return lhs.trig_time > rhs.trig_time;
    }
};

class Channel;
class Timer : public std::enable_shared_from_this<Timer> {
    int fd_;
    // loop_在此意味该类所归属线程
    EventLoop* loop_;
    std::priority_queue<TimerPacket, std::vector<TimerPacket>, TimerPacketComparator> timer_queue_;
    Channel channel_;

    void shutdown();
public:
    Timer();
    ~Timer();
    void attach_loop(EventLoop*);
    void stop();
    void handle_timer();
    void add_timer(std::chrono::steady_clock::time_point, std::function<void()>);
};