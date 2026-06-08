#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <sys/epoll.h>

namespace CHANNEL_CONFIG {
    constexpr uint32_t DEFAULT_EVENT = EPOLLRDHUP | EPOLLET;
}

class EventLoop;
class Channel {
    int fd_;
    EventLoop* loop_;
    uint32_t event_;
    uint32_t received_event_;
    bool is_tie_;
    std::weak_ptr<void> tie_;
    bool event_fresh_flag_;

    // 回调函数
    std::function<void()> read_callback_;
    std::function<void()> write_callback_;
    std::function<void()> close_callback_;
    std::function<void()> error_callback_;

    // 需要自保证外界调用时是否加锁
    void handle_event_selfguard();
    void clear_event();
public:
    Channel();
    Channel(const int&, EventLoop*);
    /*
    *   Channel暂时不设定析构行为，外界持有类需要控制进行以下操作：
    *   1. 清掉状态和监听信号，调用unregister_channel();
    *   2. 对Channel的fd置为-1，然后外界持有类需要在本身析构函数里close掉fd
    */
    ~Channel() = default;

    void set_fd(const int&);
    int get_fd() const;
    uint32_t get_event() const;
    void set_received_event(const uint32_t&);
    
    void set_read_callback(const std::function<void()>&);
    void set_write_callback(const std::function<void()>&);
    void set_close_callback(const std::function<void()>&);
    void set_error_callback(const std::function<void()>&);
    void set_tie(std::weak_ptr<void>);

    void enable_reading_notify();
    void disable_reading_notify();
    void enable_writting_notify();
    void disable_writting_notify();
    void enable_close_notify();
    void disable_close_notify();
    // 更新在epoll中的行为
    void attatch_to_loop(EventLoop*);
    void update_channel();
    void unregister_channel();
    void register_channel();

    void handle_event();
    void close_channel();
};