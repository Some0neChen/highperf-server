#pragma once

#include "EventHandler.h"
#include "Logger.h"
#include "ServerPub.h"
#include "ThreadPool.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>

struct TaskPacket {
    std::shared_ptr<std::array<char, ClientHandler::BUFFER_SIZE>> buffer;
    int fd;
    size_t len;
    TaskPacket(const std::shared_ptr<std::array<char, ClientHandler::BUFFER_SIZE>>& buffer, const int& fd, const size_t& len) : 
        buffer(buffer), fd(fd), len(len) {}
};

class TaskManager {
    std::shared_ptr<ThreadPool<std::function<EVENT_STATUS()>>> thread_pool_;
    std::queue<std::shared_ptr<TaskPacket>, std::deque<std::shared_ptr<TaskPacket>>> task_queue_;
    std::mutex mutex_;
    std::condition_variable trigger_;
    std::atomic<bool> running_state_;
    TaskManager() {
        thread_pool_ = nullptr;
    }
public:
    static TaskManager& get_manager() {
        static TaskManager G_MANAGER;
        return G_MANAGER;
    }

    ~TaskManager() {
        running_state_.store(false);
        trigger_.notify_all();
    }

    void task_listen_thread_func() {
        while (running_state_.load()) {
            std::unique_lock<std::mutex> lock(mutex_);
            trigger_.wait(lock, [this]() {
                return running_state_.load() && !task_queue_.empty();
            });
            while (!task_queue_.empty()) {
                auto task = std::move(task_queue_.front());
                task_queue_.pop();
                thread_pool_->add_task([this, task]() {
                    return task_handle(task);
                });
            }
        }
    }

    void register_thread_pool(std::shared_ptr<ThreadPool<std::function<EVENT_STATUS()>>> thread_pool) {
        this->thread_pool_ = thread_pool;
        running_state_ = true;
        this->thread_pool_->add_task([this] () {
            this->task_listen_thread_func();
            return EVENT_STATUS::OK;
        });
    }

    void add_task(std::shared_ptr<std::array<char, ClientHandler::BUFFER_SIZE>> buf, int fd, size_t len) {
        LOG_INFO("fd[%d] push task into queue. buf len %d", fd, len);
        std::lock_guard<std::mutex> lock(this->mutex_);
        task_queue_.push(std::make_shared<TaskPacket>(buf, fd, len));
        trigger_.notify_one();
    }
};