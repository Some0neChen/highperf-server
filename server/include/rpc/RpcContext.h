#pragma once
#include <cstddef>
#include <functional>
#include <memory>
#include <unordered_map>

template<typename T>
class Buffer;
class OutputChunk;
class TCPConnection;
class TLVData;
class RpcContext : public std::enable_shared_from_this<RpcContext> {
    size_t request_id_;
    size_t next_written_chunk_id_;
    std::weak_ptr<TCPConnection> conn_;
    std::unordered_map<size_t, std::shared_ptr<OutputChunk>> respond_chunks_;
    std::function<void(std::function<void()>)> dispatch_request_handle;

    bool parse_able(std::shared_ptr<Buffer<char>>);
    void save_respond_chunk(std::shared_ptr<OutputChunk>, const size_t&);
public:
    RpcContext();

    void ParseInput(std::shared_ptr<Buffer<char>> buffer);

    void set_dispatch_request_handle(std::function<void(std::function<void()>)>);
    void set_conn(std::weak_ptr<TCPConnection>);
};