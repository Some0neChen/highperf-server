#pragma once
#include <array>
#include <bits/types/struct_iovec.h>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "HttpModule.h"
#include "ServerPub.h"

class OutgoingResponse;
class HttpContext {
private:
    std::size_t next_response_id_;
    std::size_t next_request_id_;
    // 响应任务包，使用map维护写回的顺序，OutgoingResponse里负责IO
    std::unordered_map<size_t, std::shared_ptr<OutgoingResponse>> response_map_;
    // 解析任务包，里面维护与ClientHandler共享的缓冲区，并有解析结果缓冲区，以及当前状态机执行状态
    RequestHandlerPacket request_packet_;
    // http解析完成后的回调函数，交由上层处理，类里不持有线程池
    std::function<void(const size_t&, std::shared_ptr<RequestContent>&)> request_ready_callback_;
public:
    HttpContext(std::shared_ptr<RequestBuffer<char>>,
        std::function<void(const size_t&, std::shared_ptr<RequestContent>&)>);
    ParseResult HttpParseInput();

    EVENT_STATUS push_response_in_queue(std::shared_ptr<OutgoingResponse>&);
    std::shared_ptr<OutgoingResponse> get_response_in_queue();
    EVENT_STATUS update_response_queue();
    EVENT_STATUS pop_response_in_queue();
    bool all_response_done() const;
};

enum class TCPWriteResult {
    COMPLETE,
    PARTIAL,
    ERROR
};

struct StringResponseSpec {
    std::string_view version_;
    std::string_view status_;
    std::string_view content_type_;
    size_t body_len_;
    bool keep_alive_;
};

class OutgoingResponse {
public:
    size_t response_id_;
    size_t written_bytes_;
    size_t pending_write_bytes_;
    bool keep_alive_;
    
    OutgoingResponse() : response_id_(0), written_bytes_(0), pending_write_bytes_(0) {}
    OutgoingResponse(const size_t& no) : response_id_(no), written_bytes_(0), pending_write_bytes_(0) {}
    virtual ~OutgoingResponse() = default;

    virtual TCPWriteResult wirteToSocket(const int& fd) = 0;
    void append_common_headers(const StringResponseSpec&);
protected:
    
    virtual OutgoingResponse& append(const std::string_view&) = 0;
};

class StringResponse : public OutgoingResponse{
    std::string response_data_;
public:
    StringResponse(const size_t& no);
    StringResponse& append(std::string&&);
    StringResponse& append(const std::string_view&) override;
    virtual TCPWriteResult wirteToSocket(const int& fd) override;
    virtual ~StringResponse() = default;
};

class MmapFileResponse : public OutgoingResponse {
    size_t heder_size_;
    std::string header_data_;
    void* mmap_addr_;
    size_t file_size_;
    std::array<iovec, 2> iov_;
    friend class HttpGETFileSender;
public:
    MmapFileResponse(const size_t& no) : heder_size_(0), mmap_addr_(nullptr), file_size_(0), OutgoingResponse(no) {}
    MmapFileResponse& append(const std::string_view&) override;
    virtual TCPWriteResult wirteToSocket(const int& fd) override;
    virtual ~MmapFileResponse() override;
};