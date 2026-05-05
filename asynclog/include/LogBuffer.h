#pragma once

#include <bits/types/struct_iovec.h>
#include <climits>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>
#include "LogPub.h"

class LogBuffer {
    constexpr static size_t BUFFER_POOL_NUM = 4;
    constexpr static size_t BUFFER_POOL_LOG_NUM_MAX = 1024;
    std::vector<std::shared_ptr<std::vector<std::string>>> log_buffer_;
    std::queue<std::shared_ptr<std::vector<std::string>>, std::deque<std::shared_ptr<std::vector<std::string>>>> available_buffer_pool_;
    std::queue<std::shared_ptr<std::vector<std::string>>, std::deque<std::shared_ptr<std::vector<std::string>>>> flushing_buffer_pool_;
    std::mutex log_buffer_mutex_;
    std::condition_variable flush_trigger_;
    bool buffer_enable_;
    
    LogBuffer(const LogBuffer&) = delete;
    LogBuffer& operator=(const LogBuffer&) = delete;
    LogBuffer(LogBuffer&&) = delete;
    LogBuffer& operator=(LogBuffer&&) = delete;
    void check_buffer_flushing_locked();
    void force_swap_buffer_locked();
    // 提供给LogFlusher的接口，使得缓冲区的锁与条件变量都在类内管理，定时落盘，队列满时落盘
    std::shared_ptr<std::vector<std::string>> get_flushing_buffer();
    void recycle_buffer(std::shared_ptr<std::vector<std::string>> buffer);
    void stop();
    BUFFER_TRIGGER_STATE wait_for_flush_or_timeout(const size_t&);

    friend class LogFlusher;
public:
    LogBuffer();
    // 析构函数交由Logger类控制
    // 此处做冗余控制，如果Logger已经正常执行力该类内的stop()，则进入该函数后会自然返回
    ~LogBuffer() = default;
    RET_FLAG push(std::string &&log_str);
};



class LogBuffer_ {
    constexpr static size_t BUFFER_POOL_NUM = 4;
    std::vector<char> log_buffer_;
    std::queue<std::shared_ptr<std::vector<iovec>>, std::deque<std::shared_ptr<std::vector<iovec>>>> available_buffer_pool_;
    std::queue<std::shared_ptr<std::vector<iovec>>, std::deque<std::shared_ptr<std::vector<iovec>>>> flushing_buffer_pool_;
    std::queue<size_t, std::deque<size_t>> block_addr_;
    unsigned short write_pos_;
    std::mutex log_buffer_mutex_;
    std::condition_variable flush_trigger_;
    bool buffer_enable_;
    
    // LogBuffer(const LogBuffer&) = delete;
    // LogBuffer& operator=(const LogBuffer&) = delete;
    // LogBuffer(LogBuffer&&) = delete;
    // LogBuffer& operator=(LogBuffer&&) = delete;
    void check_buffer_flushing_locked();
    void force_swap_buffer_locked();
    // 提供给LogFlusher的接口，使得缓冲区的锁与条件变量都在类内管理，定时落盘，队列满时落盘
    std::shared_ptr<std::vector<std::string>> get_flushing_buffer();
    void recycle_buffer(std::shared_ptr<std::vector<std::string>> buffer);
    void stop();
    BUFFER_TRIGGER_STATE wait_for_flush_or_timeout(const size_t&);

    friend class LogFlusher;
public:
    LogBuffer_() {
        log_buffer_.reserve(LOG_SPEC::BUFFER_POOL_INIT_SIZE * IOV_MAX * LOG_SPEC::SINGLE_LOG_LEN);
        log_buffer_.resize(LOG_SPEC::BUFFER_POOL_INIT_SIZE * IOV_MAX * LOG_SPEC::SINGLE_LOG_LEN);
        for (size_t i = 0; i < LOG_SPEC::BUFFER_POOL_INIT_SIZE; ++i) {
            available_buffer_pool_.push(std::make_shared<std::vector<iovec>>());
            available_buffer_pool_.back()->reserve(IOV_MAX);
            available_buffer_pool_.back()->resize(IOV_MAX);
            block_addr_.push(i * IOV_MAX * LOG_SPEC::SINGLE_LOG_LEN);
        }
        buffer_enable_ = true;
    };

    // 析构函数交由Logger类控制
    // 此处做冗余控制，如果Logger已经正常执行力该类内的stop()，则进入该函数后会自然返回
    ~LogBuffer_() = default;
    RET_FLAG push(std::string &&log_str) {
        std::lock_guard<std::mutex> lock(log_buffer_mutex_);
        if (!buffer_enable_) {
            return RET_FLAG::UNENABLE;
        }
        if (available_buffer_pool_.empty()) {
            return RET_FLAG::ERR;
        }
        available_buffer_pool_.front()->push_back(std::move(log_str));
        check_buffer_flushing_locked();
        return RET_FLAG::OK;
    }
};