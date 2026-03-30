#pragma once

#include "Logger.h"
#include <algorithm>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <semaphore.h>
#include <stack>
#include <sys/socket.h>
#include <vector>

class EventHandler;
template <typename T>
class BufferPool
{
    std::mutex mutex_;
    std::vector<std::unique_ptr<T>> pool_storage_; // 消息内存实际存放位置
    std::stack<T*, std::deque<T*>> free_stack_; // 可用内存地址栈

    void enlarge_capacity() {
        LOG_INFO("Capacity enlarged!");
        size_t old_cap;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            old_cap = pool_storage_.size();
        }

        size_t add_cap = old_cap >> 1;
        std::vector<std::unique_ptr<T>> add_buf;
        add_buf.reserve(add_cap);
        for (int i = 0; i < add_cap; ++i) {
            add_buf.push_back(std::make_unique<T>());
        }

        std::lock_guard<std::mutex> lock(mutex_);
        std::for_each(add_buf.begin(), add_buf.end(), [this](std::unique_ptr<T>& buf) {
            pool_storage_.push_back(std::move(buf));
            free_stack_.push(pool_storage_.back().get());
        });
    }

    void recycle_buffer(T* buf) {
        std::lock_guard<std::mutex> lock(mutex_);
        free_stack_.push(buf);
        return;
    }
public:
    BufferPool(const size_t& init_capacity = 1024) {
        pool_storage_.reserve(init_capacity);
        for (int i = 0; i < init_capacity; ++i) {
            pool_storage_.push_back(std::make_unique<T>());
            free_stack_.push(pool_storage_.back().get());
        }
    }

    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;
    BufferPool(BufferPool&&) = delete;
    BufferPool& operator=(BufferPool&&) = delete;
    ~BufferPool() = default;

    std::shared_ptr<T> get_empty_buffer() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!free_stack_.empty()) {
                auto avail_buffer = free_stack_.top();
                free_stack_.pop();
                return std::shared_ptr<T>(avail_buffer, [this](T* task){
                    this->recycle_buffer(task);
                });
            }
        }
        enlarge_capacity();
        return get_empty_buffer(); // 妙
    }
};
