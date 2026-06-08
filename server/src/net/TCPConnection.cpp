#include "TCPConnection.h"
#include "Buffer.h"
#include "Channel.h"
#include "EventLoop.h"
#include "Logger.h"
#include "OutputChunk.h"
#include "Timer.h"
#include <chrono>
#include <functional>
#include <memory>
#include <unistd.h>
#include <utility>

TCPConnection::TCPConnection(const int& fd, EventLoop* loop) :
    fd_(fd),
    channel_(fd, loop),
    read_buffer_(std::make_shared<Buffer<char>>()),
    is_blocking_(false),
    close_wait_(false),
    version_(0),
    pending_output_bytes_(0),
    enable_state_(false)
{
    run_in_loop = [loop](std::function<void()> task) {
        loop->run_in_loop_thread(task);
    };
}

TCPConnection::~TCPConnection()
{
    if (fd_ != -1) {
        close(fd_);
        fd_ = -1;
    }
}

void TCPConnection::set_close_notify(std::function<void()> callback)
{
    close_notify = std::move(callback);
}

void TCPConnection::set_parse_ready_callback(std::function<void(std::weak_ptr<TCPConnection>)> callback)
{
    parse_ready_callback_ = std::move(callback);
}

void TCPConnection::set_timer_refresh_callback_(std::function<void(std::chrono::steady_clock::time_point, std::function<void()>)> callback)
{
    timer_task_refresh_callback_ = std::move(callback);
}

std::shared_ptr<Buffer<char>> TCPConnection::get_waitting_parse_data()
{
    return read_buffer_;
}

std::shared_ptr<void> TCPConnection::get_context()
{
    return context_;
}

void TCPConnection::init_channel()
{
    channel_.set_tie(weak_from_this());
    channel_.set_read_callback([this]() {
        return read_channel_buffer();
    });
    channel_.set_close_callback([this]() {
        return shutdown();
    });
    channel_.set_write_callback([this]() {
        return send();
    });
    channel_.set_error_callback([this]() {
        return force_close();
    });
    channel_.enable_close_notify();
    channel_.enable_reading_notify();
    channel_.register_channel();
    update_expired_time();
    enable_state_ = true;
}

void TCPConnection::set_context(std::shared_ptr<void> context)
{
    context_ = std::move(context);
}

void TCPConnection::shutdown()
{
    if (channel_.get_event() & EPOLLIN) {
        channel_.disable_reading_notify();
        channel_.update_channel();
    }
    close_wait_ = true;
    if (pending_output_bytes_ > 0) {
        return;
    }
    if (pending_output_bytes_ == 0 && close_wait_) {
        force_close();
    }
}

void TCPConnection::force_close()
{
    enable_state_ = false;
    channel_.unregister_channel();
    if (fd_ != -1) {
        close(fd_);
        fd_ = -1;
    }
    this->close_notify();
    return;
}

void TCPConnection::update_expired_time()
{
    ++version_;
    auto cur_version = version_;
    auto expire_time = std::chrono::steady_clock::now() + std::chrono::seconds(TIMER_SPEC::SERVER_INTERVAL);
    auto weak_self = weak_from_this();
    timer_task_refresh_callback_(expire_time, [weak_self, cur_version]() {
        auto self = weak_self.lock();
        if (!self || self->version_ != cur_version) {
            return;
        }
        self->force_close();
    });
}

// TCP管道读事件
void TCPConnection::read_channel_buffer()
{
    if (!enable_state_) {
        return;
    }
    while (true) {
        // 采用readv分散读的方式，先将数据读入缓冲区剩余空间，如果数据长度超过剩余空间，则将溢出部分读入extra_buf中
        iovec read_vec[2];
        read_vec[0].iov_base = read_buffer_->get_data() + read_buffer_->get_write_pos();
        read_vec[0].iov_len = read_buffer_->writable_size();
        char extra_buf[65536];
        read_vec[1].iov_base = extra_buf;
        read_vec[1].iov_len = sizeof(extra_buf);
        // len不能用size_t，因为size_t本质是无符号数，没有-1
        ssize_t len = readv(fd_, read_vec, 2); 

        // 读取到FIN报文
        if (len == 0) {
            LOG_INFO("TCPConnection fd[%d] closed connection.", fd_);
            shutdown();
            break;
        }

        // 已读完或者发生错误
        if (len == -1) {
            if (errno == EINTR) {
                LOG_INFO("System interrupt client fd[%d] reading. errono[%d]", fd_, errno);
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                LOG_INFO("TCPConnection fd[%d] readding over.", fd_);
                break;
            }
            LOG_INFO("TCPConnection fd[%d] occur readding failed. errorno[%d]", fd_, errno);
            return;
        }

        // 读取数据过大，直接关闭连接
        if (len > TCP_CONNECTION_SPEC::TCP_READ_SIZE_UPPER_LIMIT) {
            LOG_INFO("TCPConnection fd[%d] receive msg too large. len %u", fd_, len);
            shutdown();
            return;
        }

        // 更新缓冲区写入位置
        read_buffer_->update_write_pos(len, extra_buf);
        LOG_INFO("TCPConnection fd[%d] receive msg success, len %u", fd_, len);
    }
    // LOG_INFO("TCPConnection[%d] Get Text:\n%s", fd_, read_buffer_->get_data());
    parse_ready_callback_(weak_from_this());
    update_expired_time();
    return;
}

// TCP管道写事件
void TCPConnection::send()
{
    if (!enable_state_) {
        return;
    }
    while (!output_chunks_.empty()) {
        auto ret = output_chunks_.front()->writeToSocket(fd_);
        if (ret.write_result == TCPWriteResult::ERROR) {
            LOG_ERR("TCPConnection fd[%d] write response to socket failed.", fd_);
            this->force_close();
            return;
        }

        if (ret.write_result == TCPWriteResult::PARTIAL) {
            channel_.enable_writting_notify();
            break;
        }
        // 剩下的为返回TCPWriteResult::COMPLETE处理逻辑
        pending_output_bytes_ -= ret.written_bytes;
        output_chunks_.pop();
    }
    if (output_chunks_.empty()) {
        channel_.disable_writting_notify();
    }
    apply_backpressure();
    channel_.update_channel();
    if (pending_output_bytes_ == 0 && close_wait_) {
        force_close();
    }
}

void TCPConnection::send(std::shared_ptr<OutputChunk> chunk)
{
    pending_output_bytes_ += chunk->pending_write_bytes_;
    output_chunks_.push(chunk);
    LOG_INFO("TCPConnection fd[%d] send back response. wait_writting_bytes [%zu].",
        fd_, pending_output_bytes_);
    return send();
}

void TCPConnection::apply_backpressure()
{
    // 是否开启EPOLLIN当前只由反压机制决定
    // 超过反压水线，触发阻塞
    if (pending_output_bytes_ >= TCP_CONNECTION_SPEC::TCP_BLOCK_LINE && !is_blocking_) {
        LOG_INFO("TCPConnection fd[%d] enter traffic backpressing. wait_writting_bytes [%zu].",
            this->fd_, pending_output_bytes_);
        channel_.disable_reading_notify();
        is_blocking_ = true;
    }
    // 低于反压水线，解除阻塞
    if (pending_output_bytes_ < TCP_CONNECTION_SPEC::TCP_UNBLOCK_LINE && is_blocking_) {
        LOG_INFO("TCPConnection fd[%d] relieve traffic backpressing. wait_writting_bytes [%zu].",
            this->fd_, pending_output_bytes_);
        is_blocking_ = false;
    }

    if (!is_blocking_) {
        channel_.enable_reading_notify();
    }
    return;
}