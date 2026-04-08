#pragma once

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <memory>
#include <mutex>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <functional>
#include "Logger.h"
#include "ServerPub.h"
#include "ThreadPool.h"
#include "EventHandler.h"

class Reactor
{
    std::unordered_map<int, std::shared_ptr<EventHandler>> connections_;
    std::mutex conn_mutex_;
    size_t conn_size_;
    int epoll_fd_;
    int wake_fd_;
    std::atomic<bool> reactor_running_state_;
    std::thread reactor_thread_;
    std::shared_ptr<ThreadPool<std::function<EVENT_STATUS()>>> thread_pool_;
    constexpr static size_t MAX_EVENTS = 1024;

    void reactor_running_thread() {
        epoll_event events[MAX_EVENTS];
        std::function<EVENT_STATUS(unsigned int)> task;
        LOG_INFO("Reactor[%d] running.", this->epoll_fd_);
        while (reactor_running_state_) {
            auto trig_times = epoll_wait(this->epoll_fd_, events, MAX_EVENTS, -1);
            for (int i = 0; i < trig_times; ++i) {
                if (events[i].data.fd == this->wake_fd_) {
                    return;
                }
                std::shared_ptr<EventHandler> handler;
                {
                    std::lock_guard<std::mutex> lock(conn_mutex_);
                    auto find_iter = connections_.find(events[i].data.fd);
                    if (find_iter == connections_.end() || find_iter->second == nullptr) { // at会抛出异常，此处用find
                        LOG_ERR("Reactor[%d] find handler[%d] err.", this->epoll_fd_, events[i].data.fd);
                        continue;
                    }
                    handler = find_iter->second;
                }
                auto state = events[i].events;
                thread_pool_->add_task([this, handler, state]() {
                    return handler->handle_event(state);
                });
            }
        }
    }

    // 未使用
    std::shared_ptr<EventHandler> operator[](const int& fd) {
        if (connections_.find(fd) == connections_.end()) {
            return nullptr;
        }
        return connections_[fd];
    }
public:
    Reactor(std::shared_ptr<ThreadPool<std::function<EVENT_STATUS()>>>& thread_pool) :
        thread_pool_(thread_pool) {
        // 规定该reactor最多可接待的连接数，超过后将自动扩容
        conn_size_ = 1024;

        // epoll 创建及监听wake_fd，reactor触发析构时通过wake_fd来结束监听线程
        epoll_fd_ = epoll_create(1);
        if (epoll_fd_ == -1) {
            LOG_ERR("epoll create err.");
            exit(EXIT_FAILURE);
        }
        this->wake_fd_ = eventfd(0, EFD_NONBLOCK);
        if (wake_fd_ == -1) {
            LOG_ERR("Reactor[%d] create wakefd err. errorno %d.", this->epoll_fd_, errno);
            exit(EXIT_FAILURE);
        }
        epoll_event event;
        event.data.fd = this->wake_fd_;
        event.events = EPOLLIN | EPOLLET;
        epoll_ctl(this->epoll_fd_, EPOLL_CTL_ADD, this->wake_fd_, &event);
        LOG_INFO("Reactor[%d] initialization. Connection pool size %u.", epoll_fd_, conn_size_);

        connections_.reserve(conn_size_);
        reactor_running_state_.store(true);
        reactor_thread_ = std::thread(&Reactor::reactor_running_thread, this);
    }

    Reactor(const Reactor&) = delete;
    Reactor(Reactor&&) = delete;
    Reactor& operator=(const Reactor&) = delete;
    Reactor& operator=(const Reactor&&) = delete;

    void reset_connection(const int& fd) {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        if (connections_.find(fd) == connections_.end()) {
            return;
        }
        auto ret = epoll_ctl(this->epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
        if (ret == -1) {
            LOG_ERR("epoll del fd[%d] err.", this->epoll_fd_);
        }
        connections_[fd] = nullptr;
        this->connections_.erase(fd); 
    }

    ~Reactor() { // 担心不加锁有问题
        reactor_running_state_.store(false);
        int val = 1;
        write(this->wake_fd_, &val, sizeof(val)); // 唤醒epoll监听线程
        if (reactor_thread_.joinable()) {
            reactor_thread_.join();
        }
        for (auto conn_ : connections_) {
            if (conn_.second == nullptr) {
                continue;
            }
            reset_connection(conn_.first);
        }
        close(this->epoll_fd_);
        close(this->wake_fd_);
    }

    EVENT_STATUS add_connection(const int& fd, unsigned int listen_state, std::shared_ptr<EventHandler> conn) {
        // 隐患：在加锁内进行数据结构的find操作，时间复杂度最坏可能达到O(n)，这里是一个将来高延迟的可疑点
        std::lock_guard<std::mutex> lock(conn_mutex_);
        if (connections_.find(fd) != connections_.end() && connections_[fd] != nullptr) {
            LOG_ERR("Reactor[%d] add socket_fd[%d] err. The fd connection is already exist.", this->epoll_fd_, fd);
            return EVENT_STATUS::CLIENT_USING_ERR;
        }
        if (connections_.size() >= conn_size_) {
            conn_size_ += (conn_size_>>1);
            connections_.reserve(conn_size_);
            LOG_INFO("Reactor[%d] enlarge successfully, new size %u.", this->epoll_fd_, conn_size_);
        }

        epoll_event event;
        event.events = listen_state;
        event.data.fd = fd;
        auto ret = epoll_ctl(this->epoll_fd_, EPOLL_CTL_ADD, fd, &event);
        if (ret == -1) {
            LOG_ERR("Reactor[%d] add socket_fd err.", this->epoll_fd_);
            return EVENT_STATUS::EPOLL_ADD_ERR;
        }

        connections_[fd] = conn;
        return EVENT_STATUS::OK;
    }

    int get_epoll_fd() {
        return this->epoll_fd_;
    }
};