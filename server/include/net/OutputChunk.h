#pragma once

#include <array>
#include <bits/types/struct_iovec.h>
#include <cstddef>
#include <string>
#include <string_view>

namespace OUTPUT_CHUNK_SPEC {
    constexpr size_t HTTP_RESPOND_MSG_SIZE = 256;
}

enum class TCPWriteResult {
    COMPLETE,
    PARTIAL,
    ERROR
};

struct WriteOutCome {
    TCPWriteResult write_result;
    size_t written_bytes;
};

class OutputChunk {
public:
    size_t written_bytes_;
    size_t pending_write_bytes_;
    OutputChunk() :  written_bytes_(0), pending_write_bytes_(0) {}
    virtual ~OutputChunk() = default;
    virtual WriteOutCome writeToSocket(const int& fd) = 0;
    virtual OutputChunk& append(const std::string_view&) = 0;
    virtual OutputChunk& append(std::string&&) = 0;
protected:
    
};

class StringChunk : public OutputChunk{
    std::string response_data_;
public:
    StringChunk();
    StringChunk& append(std::string&&) override;
    StringChunk& append(const std::string_view&) override;
    virtual WriteOutCome writeToSocket(const int& fd) override;
    ~StringChunk() override = default;
};

class MMapFileChunk : public OutputChunk {
    size_t header_size_;
    std::string header_data_;
    size_t file_size_;
    void* mmap_addr_;
    std::array<iovec, 2> iov_;
public:
    MMapFileChunk() : header_size_(0), file_size_(0), mmap_addr_(nullptr), OutputChunk() {}
    void set_chunk(const size_t&, void*);
    MMapFileChunk& append(const std::string_view&) override;
    MMapFileChunk& append(std::string&&) override;
    virtual WriteOutCome writeToSocket(const int& fd) override;
    ~MMapFileChunk() override;
};