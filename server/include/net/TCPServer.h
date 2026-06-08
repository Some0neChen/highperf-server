#pragma once
#include "Acceptor.h"
#include "EventLoop.h"
#include "Timer.h"
#include <cstddef>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include "TCPConnection.h"

namespace TCPSERVER_SPEC {
    constexpr size_t REACTOR_POOL_SIZE = 4;
    constexpr unsigned short ROBIN_MASK = 0x3;
}

class TCPServer {
    // 记录轮询给哪个EventLoop
    size_t robin_idx_;
    
    std::unordered_map<size_t, std::shared_ptr<TCPConnection>> conn_manager_;
    std::vector<std::shared_ptr<Timer>> robin_loop_timer_;
    std::unique_ptr<Acceptor> acceptor_;
    std::vector<std::unique_ptr<EventLoop>> robin_loop_;
    std::unique_ptr<EventLoop> main_loop_;

    // 预留：TCP层业务处理类与上层TCP管道对接类要一一对应时，通过此接口挂接通知其同步创建
    std::function<void(std::shared_ptr<TCPConnection>)> upper_sync_create_callbackp;
    // 预留：TCP层接受数据后传给上层接口
    std::function<void(std::weak_ptr<TCPConnection>)> parse_ready_callback_;

    // TCP连接分发函数，Acceptor接收到新客户端fd后，通过该函数分发挂接EventLoop，供给Acceptor回调
    void channel_dispatch(const int&);
    void channel_remove(int);
public:
    TCPServer() : robin_idx_(0) {};
    ~TCPServer();

    // 传入要监听的IP地址及端口号，拉起TCP监听线程
    void start(const char*, const char*);

    void set_upper_sync_create_callback(std::function<void(std::shared_ptr<TCPConnection>)>);
    void set_parse_ready_callback(std::function<void(std::weak_ptr<TCPConnection>)> callbackFunc);
};