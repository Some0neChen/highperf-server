#pragma once

#include "EventHandler.h"
#include <cstddef>
#include <memory>
#include <mutex>
#include <sys/epoll.h>
#include <unistd.h>
#include <vector>
#include "Logger.h"
#include "ServerPub.h"

class ConnectionManager
{
    std::vector<std::shared_ptr<EventHandler>> connections_;
    std::mutex conn_mutex_;
    size_t conn_size_;
    int epoll_fd_;

    ConnectionManager() : conn_size_(1024), epoll_fd_(2) { // 标准化错误输出
        LOG_INFO("Connection manager initialization. Connection pool size %u.", conn_size_);
        connections_.reserve(conn_size_);
        connections_.resize(conn_size_);
    }
public:
    static ConnectionManager& get_manager() {
        static ConnectionManager G_MANAGER;
        return G_MANAGER;
    }

    ConnectionManager(const ConnectionManager&) = delete;
    ConnectionManager(ConnectionManager&&) = delete;
    ConnectionManager& operator=(const ConnectionManager&) = delete;
    ConnectionManager& operator=(const ConnectionManager&&) = delete;

    void reset_connection(const int& fd) {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        if (fd >= conn_size_) {
            return;
        }
        auto ret = epoll_ctl(this->epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
        if (ret == -1) {
            LOG_ERR("epoll del fd err.");
        }
        connections_[fd] = nullptr;
    }

    ~ConnectionManager() {
        for (int i = 3; i < conn_size_; ++i) {
            if (!connections_[i]) {
                continue;
            }
            reset_connection(i);
        }
    }

    void register_epoll_fd(const int& epoll_fd) {
        this->epoll_fd_ = epoll_fd;
    }

    EVENT_STATUS add_connection(const int& fd, unsigned int listen_state, std::shared_ptr<EventHandler> conn) {
        if (fd >= conn_size_) {
            std::lock_guard<std::mutex> lock(conn_mutex_);
            conn_size_ += (conn_size_>>1);
            connections_.reserve(conn_size_);
            connections_.resize(conn_size_);
            LOG_INFO("Connetion manager enlarge successfully, new size %u.", conn_size_);
        }

        epoll_event event;
        event.events = listen_state;
        event.data.fd = fd;
        auto ret = epoll_ctl(this->epoll_fd_, EPOLL_CTL_ADD, fd, &event);
        if (ret == -1) {
            LOG_ERR("epoll add socket_fd err.");
            return EVENT_STATUS::EPOLL_ADD_ERR;
        }

        connections_[fd] = conn;
        return EVENT_STATUS::OK;
    }

    std::shared_ptr<EventHandler> operator[](const int& fd) {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        if (fd >= conn_size_) {
            return nullptr;
        }
        return connections_[fd];
    }
};