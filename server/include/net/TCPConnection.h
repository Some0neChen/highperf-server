#pragma once

#include "Buffer.h"
#include "Channel.h"
#include <chrono>
#include <functional>
#include <memory>
#include <queue>

namespace TCP_CONNECTION_SPEC {
    // TCP发送阻塞的水位线，16MB
    constexpr size_t TCP_BLOCK_LINE = 1024 * 1024 * 16;
    // TCP发送解除阻塞的水位线，为高水位线一半
    constexpr size_t TCP_UNBLOCK_LINE = TCP_BLOCK_LINE >> 1;
    constexpr size_t TCP_READ_SIZE_UPPER_LIMIT = 65536;
}

class OutputChunk;
class TCPConnection : public std::enable_shared_from_this<TCPConnection> {
    int fd_;
    Channel channel_;
    std::shared_ptr<Buffer<char>> read_buffer_;
    bool is_blocking_;
    bool close_wait_;
    int version_;
    size_t pending_output_bytes_;
    bool enable_state_;
    bool timer_enable_;
    // 协议上下文
    std::shared_ptr<void> context_;
    std::queue<std::shared_ptr<OutputChunk>> output_chunks_;

    void update_expired_time();
    // 通知上层清除该TCP管道
    std::function<void()> close_notify;
    // TCP管道数据接收完毕并存储完毕后，通知上层处理
    std::function<void(std::weak_ptr<TCPConnection>)> parse_ready_callback_;
    // TCPServer提供对应定时器刷新函数，TCPConnection通过该函数注册定时器回调来实现连接过期时间的更新
    std::function<void(std::chrono::steady_clock::time_point, std::function<void()>)> timer_task_refresh_callback_;

    
    void force_close();
    void read_channel_buffer();
    void send();

    // 反压控制
    void apply_backpressure();
public:
    TCPConnection(const int&, EventLoop*);
    ~TCPConnection();
    void shutdown();
    std::function<void(std::function<void()>)> run_in_loop;
    
    void init_channel();
    // 挂接上层协议
    void set_context(std::shared_ptr<void>);
    // 设置是否开启TCP管道定时器
    void set_timer_enable(bool);
    void set_close_notify(std::function<void()>);
    void set_parse_ready_callback(std::function<void(std::weak_ptr<TCPConnection>)>);

    void set_timer_refresh_callback_(std::function<void(std::chrono::steady_clock::time_point, std::function<void()>)>);
    std::shared_ptr<Buffer<char>> get_waitting_parse_data();
    std::shared_ptr<void> get_context();
    
    void send(std::shared_ptr<OutputChunk>);
};