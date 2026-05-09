#pragma once

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <memory>
#include <mutex>
#include <queue>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <functional>
#include <vector>
#include "ServerPub.h"
#include "ThreadPool.h"
#include "EventHandler.h"

class Reactor
{
    using reactor_task = std::function<EVENT_STATUS()>;
    std::shared_ptr<ThreadPool<std::function<EVENT_STATUS()>>> thread_pool_;
    std::unordered_map<int, std::shared_ptr<EventHandler>> connections_;
    std::queue<reactor_task, std::deque<reactor_task>> pending_tasks_;
    std::mutex task_mutex_;
    std::atomic<bool> reactor_running_state_;
    std::thread reactor_thread_;
    std::shared_ptr<TimerHandler> timer_handler_;
    size_t conn_size_;
    int epoll_fd_;
    int wake_fd_;

    constexpr static size_t MAX_EVENTS = 1024;

    void reactor_running_thread();
    bool is_client_exist(const int&);
    EVENT_STATUS wake_up();
    EVENT_STATUS handle_wake_up();
    EVENT_STATUS do_pending_tasks();
    bool is_in_loop_thread();
    EVENT_STATUS stop_reactor();
    EVENT_STATUS remove_all_conns();

    // 将事件处理类设置为友元，以便其访问对应Reactor内注册的线程池
    friend class ClientHandler;
    friend class ListenHandler;
    friend class TimerHandler;
public:
    Reactor(std::shared_ptr<ThreadPool<std::function<EVENT_STATUS()>>>&);
    ~Reactor();
    Reactor(const Reactor&) = delete;
    Reactor(Reactor&&) = delete;
    Reactor& operator=(const Reactor&) = delete;
    Reactor& operator=(const Reactor&&) = delete;

    void reset_connection(const int&);
    void timer_reset_batch_conns(const std::shared_ptr<std::vector<std::pair<int, unsigned int>>>&);
    void add_timer_task(const int &fd, const unsigned int &version_no, const std::chrono::steady_clock::time_point &time_point);
    EVENT_STATUS add_connection(const int&, unsigned int, std::shared_ptr<EventHandler>);
    int get_epoll_fd() const;
    EVENT_STATUS queue_in_loop(reactor_task);
    EVENT_STATUS run_in_loop(reactor_task);
};