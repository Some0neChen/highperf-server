#pragma once

#include "LogPub.h"
#include "LogFile.h"
#include "LogBuffer.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex> // Required for std::condition_variable and std::unique_lock
#include <string>
#include <thread>
#include <vector>

class LogFlusher {
    std::condition_variable flush_trigger_;
    std::mutex mtx;
    std::thread flusher_thread;
    std::atomic<bool> running_;
    std::shared_ptr<LogFile> file;
    std::shared_ptr<LogBuffer> buffer;

    void flush_routine() {
        std::unique_lock<std::mutex> lock(mtx); // 创建时锁住，析构时释放。并且可以自定义上锁解锁。
        std::shared_ptr<std::vector<std::string>> buffer_data;
        bool triggered_status = false;
        while (running_.load()) {
            triggered_status = flush_trigger_.wait_for(lock, std::chrono::seconds(3), [this, &buffer_data]() { 
                return (buffer_data = buffer->get_flushing_buffer()) != nullptr; });
            if (triggered_status == false) { // 代表当前情况为定时器触发，需要在Buffer里主动触发数据切换
                buffer->force_swap_buffer();
                buffer_data = buffer->get_flushing_buffer(); // 触发Buffer数据存放到待刷新队列后，再次获取数据
            }
            if (buffer_data == nullptr) {
                continue;
            }
            file->write_log(buffer_data);
            buffer->recycle_buffer(buffer_data);
            buffer_data = nullptr;
        }
    }

    void force_flush() {
        std::shared_ptr<std::vector<std::string>> buffer_data;
        while ((buffer_data = buffer->get_flushing_buffer()) != nullptr) {
            file->write_log(buffer_data);
            buffer->recycle_buffer(buffer_data);
        }
    }

    void stop() {
        running_.store(false);
        flush_trigger_.notify_all();
        flusher_thread.join();
    }

    friend class Logger;
public:
    LogFlusher(std::shared_ptr<LogFile> file, std::shared_ptr<LogBuffer> buffer) : file(file), buffer(buffer) {
        buffer->register_flusher(&flush_trigger_);
        running_.store(true);
        flusher_thread = std::thread([this]() {
            this->flush_routine();
        });
    }

    ~LogFlusher() {
        stop();
    }

    LogFlusher(const LogFlusher&) = delete;
    LogFlusher& operator=(const LogFlusher&) = delete;
    LogFlusher(LogFlusher&&) = delete;
    LogFlusher& operator=(LogFlusher&&) = delete;
};