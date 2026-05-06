#pragma once

#include <bits/types/struct_iovec.h>
#include <climits>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <deque>
#include <list>
#include <memory>
#include <mutex>
#include <queue>
#include <unistd.h>
#include <vector>
#include "LogPub.h"

class LogBuffer {
    std::list<std::vector<char>> buffer_data_;  /* 实际数据存储的内存块，设计成list预留动态扩容接口 */
    std::queue<std::shared_ptr<BufferBlock>, std::deque<std::shared_ptr<BufferBlock>>> available_buffer_pool_;
    std::queue<std::shared_ptr<BufferBlock>, std::deque<std::shared_ptr<BufferBlock>>> flushing_buffer_pool_;
    std::mutex log_buffer_mutex_;
    std::condition_variable flush_trigger_;
    bool buffer_enable_;
    
    LogBuffer(const LogBuffer&) = delete;
    LogBuffer& operator=(const LogBuffer&) = delete;
    LogBuffer(LogBuffer&&) = delete;
    LogBuffer& operator=(LogBuffer&&) = delete;

    // 用于挂接新的内存块，每单位block_size对应IOV_MAX * SINGLE_LOG_LEN大小的内存块，满足单次落盘的最大容量
    void attach_log_buffer_block(const size_t&);
    void check_buffer_flushing_locked();
    void force_swap_buffer_locked();
    // 提供给LogFlusher的接口，使得缓冲区的锁与条件变量都在类内管理，定时落盘，队列满时落盘
    std::shared_ptr<BufferBlock> get_flushing_buffer();
    void recycle_buffer(std::shared_ptr<BufferBlock> buffer);
    void stop();
    BUFFER_TRIGGER_STATE wait_for_flush_or_timeout(const size_t&);

    friend class LogFlusher;
public:
    LogBuffer();
    // 析构函数交由Logger类控制
    // 此处做冗余控制，如果Logger已经正常执行力该类内的stop()，则进入该函数后会自然返回
    ~LogBuffer() = default;
    RET_FLAG push(const char*, const size_t&);
};