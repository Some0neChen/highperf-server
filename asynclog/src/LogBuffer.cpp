#include "LogBuffer.h"
#include "LogPub.h"
#include <chrono>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>

LogBuffer::LogBuffer() {
    attach_log_buffer_block(LOG_SPEC::BUFFER_POOL_INIT_SIZE);
    buffer_enable_ = true;
}

void LogBuffer::stop() {
    std::lock_guard<std::mutex> lock(log_buffer_mutex_);
    {
        if (!buffer_enable_) {
            return;
        }
        buffer_enable_ = false;
    }
    flush_trigger_.notify_all();
    return;
}

void LogBuffer::attach_log_buffer_block(const size_t& block_size) {
    buffer_data_.push_back(std::vector<char>());
    buffer_data_.back().reserve(block_size * IOV_MAX * LOG_SPEC::SINGLE_LOG_LEN);
    buffer_data_.back().resize(block_size * IOV_MAX * LOG_SPEC::SINGLE_LOG_LEN);
    for (size_t i = 0; i < block_size; ++i) {
        available_buffer_pool_.push(std::make_shared<BufferBlock>());
        available_buffer_pool_.back()->block_data_ =
            buffer_data_.back().data() + i * IOV_MAX * LOG_SPEC::SINGLE_LOG_LEN;
        available_buffer_pool_.back()->write_pos_ = 0;
        available_buffer_pool_.back()->written_bytes_ = 0;
        available_buffer_pool_.back()->block_iovec_.reserve(IOV_MAX);
        available_buffer_pool_.back()->block_iovec_.resize(IOV_MAX);
    }
}

void LogBuffer::check_buffer_flushing_locked() {
    if (available_buffer_pool_.front()->write_pos_ < IOV_MAX) {
        return;
    }
    force_swap_buffer_locked();
    flush_trigger_.notify_all();
    return;
}

// 提供给LogFlusher的接口，用于定时落盘时，主动把数据切换到要写入的队列中
void LogBuffer::force_swap_buffer_locked() {
    if (available_buffer_pool_.empty() || available_buffer_pool_.front()->write_pos_ == 0) {
        return;
    }
    flushing_buffer_pool_.push(available_buffer_pool_.front());
    available_buffer_pool_.pop();
    return;
}

std::shared_ptr<BufferBlock> LogBuffer::get_flushing_buffer() {
    std::lock_guard<std::mutex> lock(log_buffer_mutex_);
    if (flushing_buffer_pool_.empty()) {
        return nullptr;
    }
    auto ret = flushing_buffer_pool_.front();
    flushing_buffer_pool_.pop();
    return ret;
}

void LogBuffer::recycle_buffer(std::shared_ptr<BufferBlock> buffer) {
    // The block is owned only by the flusher before it is pushed back
    // to available_buffer_pool_, so resetting write_pos_ here is safe.
    buffer->write_pos_ = 0;
    buffer->written_bytes_ = 0;
    std::lock_guard<std::mutex> lock(log_buffer_mutex_);
    available_buffer_pool_.push(buffer);
}

RET_FLAG LogBuffer::push(const char* log_str, const size_t& log_len) {
    std::lock_guard<std::mutex> lock(log_buffer_mutex_);
    if (!buffer_enable_) {
        return RET_FLAG::UNENABLE;
    }
    if (available_buffer_pool_.empty()) {
        return RET_FLAG::ERR;
    }
    if (log_len > LOG_SPEC::SINGLE_LOG_LEN) {
        return RET_FLAG::ERR;
    }
    auto& current_buffer = available_buffer_pool_.front();
    current_buffer->block_iovec_[current_buffer->write_pos_].iov_base =
        current_buffer->block_data_ + current_buffer->write_pos_ * LOG_SPEC::SINGLE_LOG_LEN;
    current_buffer->block_iovec_[current_buffer->write_pos_].iov_len = log_len;
    current_buffer->written_bytes_ += log_len;
    memcpy(current_buffer->block_iovec_[current_buffer->write_pos_].iov_base, log_str, log_len);
    ++current_buffer->write_pos_;
    check_buffer_flushing_locked();
    return RET_FLAG::OK;
}

BUFFER_TRIGGER_STATE LogBuffer::wait_for_flush_or_timeout(const size_t& interval) {
    std::unique_lock<std::mutex> lock(log_buffer_mutex_);
    bool buf_trigged = flush_trigger_.wait_for(lock, std::chrono::seconds(interval), [this]() {
        return !buffer_enable_ || !flushing_buffer_pool_.empty();
    });
    // 程序退出兜底落盘
    if (!buffer_enable_) {
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