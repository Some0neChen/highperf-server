#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <sys/epoll.h>
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
    std::atomic<bool> reactor_running_state_;
    std::shared_ptr<ThreadPool<std::function<EVENT_STATUS()>>> thread_pool_;
    constexpr static size_t MAX_EVENTS = 1024;

    void reactor_running_thread() {
        epoll_event events[MAX_EVENTS];
        std::function<EVENT_STATUS(unsigned int)> task;
        LOG_INFO("Reactor[%d] running.", this->epoll_fd_);
        while (reactor_running_state_) {
            auto trig_times = epoll_wait(this->epoll_fd_, events, MAX_EVENTS, -1);
            for (int i = 0; i < trig_times; ++i) {
                std::lock_guard<std::mutex> lock(conn_mutex_);
                if (connections_.at(events[i].data.fd) == nullptr) {
                    continue;
                }
                auto handler = connections_.at(events[i].data.fd);
                auto state = events[i].events;
                thread_pool_->add_task([this, handler, state]() {
                    return handler->handle_event(state);
                });
            }
        }
    }
public:
    Reactor(std::shared_ptr<ThreadPool<std::function<EVENT_STATUS()>>>& thread_pool) :
        thread_pool_(thread_pool) {
        conn_size_ = 1024;
        epoll_fd_ = epoll_create(1);
        if (epoll_fd_ == -1) {
            LOG_ERR("epoll create err.");
            exit(EXIT_FAILURE);
        }
        LOG_INFO("Reactor[%d] initialization. Connection pool size %u.", epoll_fd_, conn_size_);
        connections_.reserve(conn_size_);
        reactor_running_state_.store(true);
        thread_pool_->add_task([this](){
            this->reactor_running_thread();
            return EVENT_STATUS::OK;
        });
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
        for (auto conn_ : connections_) {
            if (conn_.second == nullptr) {
                continue;
            }
            reset_connection(conn_.first);
        }
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

    std::shared_ptr<EventHandler> operator[](const int& fd) {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        if (connections_.find(fd) == connections_.end()) {
            return nullptr;
        }
        return connections_[fd];
    }

    int get_epoll_fd() {
        return this->epoll_fd_;
    }
};