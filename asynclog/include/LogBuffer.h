#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "LogPub.h"

class LogBuffer {
    constexpr static size_t BUFFER_POOL_NUM = 4;
    constexpr static size_t BUFFER_POOL_LOG_NUM_MAX = 1024;
    std::vector<std::shared_ptr<std::vector<std::string>>> log_buffer;
    std::queue<std::shared_ptr<std::vector<std::string>>, std::deque<std::shared_ptr<std::vector<std::string>>>> available_buffer_pool;
    std::queue<std::shared_ptr<std::vector<std::string>>, std::deque<std::shared_ptr<std::vector<std::string>>>> flushing_buffer_pool;
    std::mutex log_buffer_mutex;
    std::condition_variable* flush_trigger;
    
    LogBuffer(const LogBuffer&) = delete;
    LogBuffer& operator=(const LogBuffer&) = delete;
    LogBuffer(LogBuffer&&) = delete;
    LogBuffer& operator=(LogBuffer&&) = delete;
    void stop() {
        {
            std::lock_guard<std::mutex> lock(log_buffer_mutex);
            while (!available_buffer_pool.empty()) {
                flushing_buffer_pool.push(available_buffer_pool.front());
                available_buffer_pool.pop();
            }
        }
        flush_trigger->notify_all();
    }
    void check_buffer_flushing() {
        if (available_buffer_pool.front()->size() < BUFFER_POOL_LOG_NUM_MAX) {
            return;
        }
        flushing_buffer_pool.push(available_buffer_pool.front());
        available_buffer_pool.pop();
        flush_trigger->notify_all();
        return;
    }
    void trigger_flush() {
        flush_trigger->notify_all();
    }
    void register_flusher(std::condition_variable* trigger) {
        this->flush_trigger = trigger;
    }
    void force_swap_buffer() {  //  提供给LogFlusher的接口，用于定时落盘时，主动把数据切换到要写入的队列中
        std::lock_guard<std::mutex> lock(log_buffer_mutex);
        if (available_buffer_pool.empty() || available_buffer_pool.front()->empty()) {
            return;
        }
        flushing_buffer_pool.push(available_buffer_pool.front());
        available_buffer_pool.pop();
        return;
    }
    std::shared_ptr<std::vector<std::string>> get_flushing_buffer() {
        std::lock_guard<std::mutex> lock(log_buffer_mutex);
        if (flushing_buffer_pool.empty()) {
            return nullptr;
        }
        auto ret = flushing_buffer_pool.front();
        flushing_buffer_pool.pop();
        return ret;
    }
    void recycle_buffer(std::shared_ptr<std::vector<std::string>> buffer) {
        std::lock_guard<std::mutex> lock(log_buffer_mutex);
        buffer->clear();
        available_buffer_pool.push(buffer);
    }
    friend class LogFlusher;
    friend class Logger;
public:
    LogBuffer() {
        flush_trigger = nullptr;
        for (size_t i = 0; i < BUFFER_POOL_NUM; ++i) {
            log_buffer.push_back(std::make_shared<std::vector<std::string>>());
            log_buffer.back()->reserve(BUFFER_POOL_LOG_NUM_MAX);
            available_buffer_pool.push(log_buffer.back());
        }
    }

    ~LogBuffer() { // 析构函数交由Logger类控制
        stop(); // 此处做冗余控制，如果Logger已经正常执行力该类内的stop()，则进入该函数后会自然返回
    }

    RET_FLAG push(std::string &log_str) {
        std::lock_guard<std::mutex> lock(log_buffer_mutex);
        if (available_buffer_pool.empty()) {
            return RET_FLAG::ERR;
        }
        available_buffer_pool.front()->push_back(std::move(log_str));
        check_buffer_flushing();
        return RET_FLAG::OK;
    }
};