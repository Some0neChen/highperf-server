#pragma once

#include "HttpParser.h"
#include <cstddef>
#include <functional>
#include <memory>
#include <unordered_map>

template<typename T>
class Buffer;
class RequestContent;
class OutputChunk;
class TCPConnection;
class HttpContext : public std::enable_shared_from_this<HttpContext> {
    size_t request_id_;
    size_t next_written_chunk_id_;
    std::weak_ptr<TCPConnection> conn_;
    std::unique_ptr<RequestHandlerPacket> packet_;
    std::unordered_map<size_t, std::shared_ptr<OutputChunk>> respond_chunks_;
    std::function<void(std::function<void()>)> dispatch_request_handle;
public:
    HttpContext();

    void ParseInput(std::shared_ptr<Buffer<char>> buffer);

    void set_dispatch_request_handle(std::function<void(std::function<void()>)>);
    void set_conn(std::weak_ptr<TCPConnection>);
    void save_respond_chunk(std::shared_ptr<RequestContent>, const size_t& request_id);
};