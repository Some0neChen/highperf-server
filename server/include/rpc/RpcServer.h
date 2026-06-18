#pragma once

#include "TCPConnection.h"
#include <functional>
#include <memory>
#include "ThreadPool.h"

class RpcContext;
class RpcServer {
    // RPC业务处理线程池
    ThreadPool<std::function<void()>> tpool_;
public:
    RpcServer() = default;
    ~RpcServer() = default;

    void set_rpc_context(std::shared_ptr<TCPConnection>);
    void read_tcp_buffer(std::weak_ptr<TCPConnection>);
};