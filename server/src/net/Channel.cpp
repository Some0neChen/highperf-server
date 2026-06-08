#include "Channel.h"
#include <sys/epoll.h>
#include <utility>
#include "Logger.h"
#include "EventLoop.h"

Channel::Channel() :
    fd_(-1), loop_(nullptr), event_(EPOLLET), received_event_(0x0), is_tie_(false), event_fresh_flag_(false) {}

Channel::Channel(const int& fd, EventLoop* loop) :
    fd_(fd), loop_(loop), event_(EPOLLET), received_event_(0x0), is_tie_(false), event_fresh_flag_(false) {}

void Channel::enable_reading_notify()
{
    if (event_ & EPOLLIN) {
        return;
    }
    event_fresh_flag_ = true;
    event_ |= EPOLLIN;
}

void Channel::disable_reading_notify()
{
    if (event_ & EPOLLIN) {
        event_fresh_flag_ = true;
        event_ &= ~EPOLLIN;
    }
}

void Channel::enable_writting_notify()
{
    if (event_ & EPOLLOUT) {
        return;
    }
    event_fresh_flag_ = true;
    event_ |= EPOLLOUT;
}

void Channel::disable_writting_notify()
{
    if (event_ & EPOLLOUT) {
        event_fresh_flag_ = true;
        event_ &= ~EPOLLOUT;
    }
}

void Channel::enable_close_notify()
{
    if (event_ & EPOLLRDHUP) {
        return;
    }
    event_fresh_flag_ = true;
    event_ |= EPOLLRDHUP;
}

void Channel::disable_close_notify()
{
    if (event_ & EPOLLRDHUP) {
        event_fresh_flag_ = true;
        event_ &= ~EPOLLRDHUP;
    }
}

void Channel::attatch_to_loop(EventLoop* loop)
{
    loop_ = loop;
    return;
}

void Channel::update_channel()
{
    if (!loop_) {
        LOG_ERR("Channel[%d] unbind loop excute update_channel failed.", fd_);
        return;
    }
    if (!event_fresh_flag_) {
        return;
    }
    loop_->update_channel(this);
    event_fresh_flag_ = false;
    return;
}

void Channel::unregister_channel()
{
    clear_event();
    if (loop_) {
        loop_->unregister_channel(this);
        event_fresh_flag_ = false;
        return;
    }
    LOG_ERR("Channel[%d] unbind loop excute unregister_channel failed.", fd_);
}

void Channel::register_channel()
{
    if (loop_) {
        loop_->register_channel(this);
        event_fresh_flag_ = false;
        return;
    }
    LOG_ERR("Channel[%d] unbind loop excute register_channel failed.", fd_);
}

void Channel::set_fd(const int& fd)
{
    fd_ = fd;
    return;
}

int Channel::get_fd() const
{
    return fd_;
}

uint32_t Channel::get_event() const
{
    return event_;
}

void Channel::set_received_event(const uint32_t& event)
{
    received_event_ = event;
    return;
}

void Channel::set_read_callback(const std::function<void()>& func)
{
    read_callback_ = std::move(func);
    return;
}

void Channel::set_write_callback(const std::function<void()>& func)
{
    write_callback_ = std::move(func);
    return;
}

void Channel::set_close_callback(const std::function<void()>& func)
{
    close_callback_ = std::move(func);
    return;
}

void Channel::set_error_callback(const std::function<void()>& func)
{
    error_callback_ = std::move(func);
    return;
}

void Channel::set_tie(std::weak_ptr<void> tie)
{
    is_tie_ = true;
    tie_ = tie;
    return;
}

void Channel::clear_event()
{
    event_ = 0x0;
    received_event_ = 0x0;
    event_fresh_flag_ = true;
    return;
}

void Channel::close_channel()
{
    if (close_callback_) {
        close_callback_();
    }
}

void Channel::handle_event()
{
    if (is_tie_) {
        auto tie = tie_.lock();
        if (!tie) {
            return;
        }
        handle_event_selfguard();
        return;
    }
    handle_event_selfguard();    
}

void Channel::handle_event_selfguard()
{
    if (!(received_event_ & event_)) {
        LOG_INFO("Channel fd[%d] trig unexpected event.", fd_);
        if (error_callback_) {
            error_callback_();
        }
        return;
    }
    if (received_event_ & (EPOLLERR | EPOLLHUP)) {
        if (error_callback_) {
            error_callback_();
        }
        return;
    }
    if (received_event_ & EPOLLOUT) {
        if (write_callback_) {
            write_callback_();
        }
    }
    if (received_event_ & EPOLLIN) {
        if (read_callback_) {
            read_callback_();
        }
    }
    if (received_event_ & EPOLLRDHUP) { // 客户端已发送FIN
        if (close_callback_) {
            close_callback_();
        }
    }
    return;
}