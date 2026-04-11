#pragma once

#include "Logger.h"
#include "ServerPub.h"
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

template <typename T>
class RequestBuffer_
{
public:
    using BUF_SIZE = typename std::vector<T>::size_type;
    RequestBuffer_() : write_pos_(0), read_pos_(0) {
        buffer_.resize(SPECS_VALUE::STANDARD_REQUEST_BUF_SIZE);
    }

    BUF_SIZE writable_size() const {
        return buffer_.size() - write_pos_;
    }

    BUF_SIZE readable_size() const {
        return buffer_.size() - read_pos_;
    }

    T* get_data() {
        return buffer_.data();
    }

    BUF_SIZE get_write_pos() const {
        return  write_pos_;
    }

    BUF_SIZE get_read_pos() const {
        return read_pos_;
    }

    void update_write_pos(const BUF_SIZE& len, char* temp_buf) {
        // 如果当前写入数据长度不超过可写内存，则直接写入
        if (len <= writable_size()) {
            write_pos_ += len;
            return;
        }

        // 获取溢出的数据长度
        BUF_SIZE extra_len = len - writable_size();

        // 先将待读内存数据搬移到缓冲区起始位置
        LOG_INFO("Forward move buffer. oldlen[%u], move size[%u]", write_pos_, buffer_.size() - write_pos_);
        auto write_it = std::copy(buffer_.begin() + read_pos_, buffer_.end(), buffer_.begin());
        read_pos_ = 0;

        // 如果待读内存大小加上要写入的内存大小小于缓冲区总大小
        // 则将待读内存数据搬移到缓冲区起始位置，并将新数据写入剩余空间
        if (write_it + extra_len < buffer_.end()) {
            write_it = std::copy(temp_buf, temp_buf + extra_len, write_it);
            write_pos_ = write_it - buffer_.begin();
            return;
        }

        // 否则需要扩容缓冲区，将新数据写入剩余空间
        BUF_SIZE old_size = buffer_.size();
        buffer_.resize(old_size + extra_len);
        LOG_INFO("Enlarge buffer. oldSize [%u], newSize [%u], writeSize [%u]", old_size, buffer_.size(), extra_len);
        // 注意resize后迭代器失效，所以需要重新获取写入位置
        write_it = std::copy(temp_buf, temp_buf + extra_len, buffer_.begin() + old_size);
        write_pos_ = write_it - buffer_.begin();
    }

    void update_read_pos(const BUF_SIZE& read_len) {
        read_pos_ += read_len;
        pos_reset();
        shrink_buffer();
    }
private:
    std::vector<T> buffer_;
    BUF_SIZE write_pos_;
    BUF_SIZE read_pos_;

    RequestBuffer_(const RequestBuffer_&) = delete;
    RequestBuffer_(RequestBuffer_&&) = delete;
    RequestBuffer_& operator=(const RequestBuffer_&) = delete;
    RequestBuffer_& operator=(RequestBuffer_&&) = delete;

    void pos_reset() {
        if (read_pos_ == write_pos_) {
            read_pos_ = 0;
            write_pos_ = 0;
            LOG_INFO("Reset write_pos and read_pos success.");
        }
    }

    // 如果当前内存超出64KB，且可读内存在4KB之内
    // 那么进行对应的缩容操作
    void shrink_buffer() {
        if (buffer_.size() < SPECS_VALUE::HUG_MSG_BUFFER_SIZE) {
            return;
        }
        if (readable_size() >= SPECS_VALUE::STANDARD_REQUEST_BUF_SIZE) {
            return;
        }
        std::vector<T> new_buf = std::vector<T>(buffer_.begin() + read_pos_, buffer_.begin() + write_pos_);
        new_buf.resize(SPECS_VALUE::STANDARD_REQUEST_BUF_SIZE);
        buffer_.swap(new_buf);
    }
};