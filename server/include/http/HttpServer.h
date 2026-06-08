#pragma once

#include "TCPConnection.h"
#include <functional>
#include <memory>
#include "ThreadPool.h"

class HttpContext;
class HttpServer {
    ThreadPool<std::function<void()>> tpool_;
public:
    HttpServer() = default;
    ~HttpServer() = default;

    void set_http_context(std::shared_ptr<TCPConnection>);
    void read_tcp_buffer(std::weak_ptr<TCPConnection>);
};