#include "LogBuffer.h"
#include "LogPub.h"
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>

LogBuffer::LogBuffer() {
    buffer_enable_.store(true);
    for (size_t i = 0; i < BUFFER_POOL_NUM; ++i) {
        log_buffer_.push_back(std::make_shared<std::vector<std::string>>());
        log_buffer_.back()->reserve(BUFFER_POOL_LOG_NUM_MAX);
        available_buffer_pool_.push(log_buffer_.back());
    }
}

LogBuffer::~LogBuffer() { // 析构函数交由Logger类控制
    stop(); // 此处做冗余控制，如果Logger已经正常执行力该类内的stop()，则进入该函数后会自然返回
}

void LogBuffer::stop() {
    if (!buffer_enable_.load()) {
        return;
    }
    buffer_enable_.store(false);
    flush_trigger_.notify_all();
    return;
}

void LogBuffer::check_buffer_flushing_locked() {
    if (available_buffer_pool_.front()->size() < BUFFER_POOL_LOG_NUM_MAX) {
        return;
    }
    flushing_buffer_pool_.push(available_buffer_pool_.front());
    available_buffer_pool_.pop();
    flush_trigger_.notify_all();
    return;
}

// 提供给LogFlusher的接口，用于定时落盘时，主动把数据切换到要写入的队列中
void LogBuffer::force_swap_buffer_locked() {  
    if (available_buffer_pool_.empty() || available_buffer_pool_.front()->empty()) {
        return;
    }
    flushing_buffer_pool_.push(available_buffer_pool_.front());
    available_buffer_pool_.pop();
    return;
}

std::shared_ptr<std::vector<std::string>> LogBuffer::get_flushing_buffer() {
    std::lock_guard<std::mutex> lock(log_buffer_mutex_);
    if (flushing_buffer_pool_.empty()) {
        return nullptr;
    }
    auto ret = flushing_buffer_pool_.front();
    flushing_buffer_pool_.pop();
    return ret;
}

void LogBuffer::recycle_buffer(std::shared_ptr<std::vector<std::string>> buffer) {
    buffer->clear();
    std::lock_guard<std::mutex> lock(log_buffer_mutex_);
    available_buffer_pool_.push(buffer);
}

RET_FLAG LogBuffer::push(std::string &&log_str) {
    if (!buffer_enable_.load()) {
        return RET_FLAG::UNENABLE;
    }
    std::lock_guard<std::mutex> lock(log_buffer_mutex_);
    if (available_buffer_pool_.empty()) {
        return RET_FLAG::ERR;
    }
    available_buffer_pool_.front()->push_back(std::move(log_str));
    check_buffer_flushing_locked();
    return RET_FLAG::OK;
}

BUFFER_TRIGGER_STATE LogBuffer::wait_for_flush_or_timeout(const size_t& interval) {
    std::unique_lock<std::mutex> lock(log_buffer_mutex_);
    bool buf_trigged = flush_trigger_.wait_for(lock, std::chrono::seconds(interval), [this]() {
        return !buffer_enable_.load() || !flushing_buffer_pool_.empty();
    });
    // 程序退出兜底落盘
    if (!buffer_enable_.load()) {
        this->force_swap_buffer_locked();
        return BUFFER_TRIGGER_STATE::CLOSE;
    }
    // 定时器触发
    if (!buf_trigged) {
        this->force_swap_buffer_locked();
        return BUFFER_TRIGGER_STATE::TIME_OUT;
    }
    // 数据区满落盘
    return BUFFER_TRIGGER_STATE::FLUSH;
}