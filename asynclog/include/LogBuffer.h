#pragma once

#include <atomic>
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
    std::atomic<bool> buffer_enable_;
    
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
    ~LogBuffer();
    RET_FLAG push(std::string &&log_str);
};