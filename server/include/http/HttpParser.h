#pragma once
#include "HttpPub.h"
#include <memory>
#include <functional>

// Http解析状态机挂接函数
void init_http_parse_fsm();

enum class ParseResult {
    INCOMPLETE,  // 数据不够，等下次
    COMPLETE,    // 解析完成
    ERROR        // 格式错误
};

enum class FSMState {
    START = 0,
    METHOD,
    PATH,
    VERSION,
    HEADER,
    BODY,
    END
};

template<typename T>
class Buffer;
class RequestHandlerPacket {
public:
    std::shared_ptr<Buffer<char>> data_buffer_;
    std::shared_ptr<RequestContent> content_buffer_;
    FSMState current_state_;

    RequestHandlerPacket(std::shared_ptr<Buffer<char>>);
    RequestHandlerPacket(RequestHandlerPacket&&) = default;
    RequestHandlerPacket(const RequestHandlerPacket&) = default;
    RequestHandlerPacket& operator=(RequestHandlerPacket&&) = default;
    RequestHandlerPacket& operator=(const RequestHandlerPacket&) = default;
    ~RequestHandlerPacket() = default;

    // 将解析完的http数据返回，并分配新的堆区用来存取下一个http数据
    std::shared_ptr<RequestContent> pop_content();
};


// Http解析状态机
class HttpParser {
    using StateHandler = std::function<ParseResult(RequestHandlerPacket&)>;
    std::vector<StateHandler> state_handlers_;

    HttpParser() = default;
    HttpParser(const HttpParser&) = delete;
    HttpParser(HttpParser&&) = delete;
    HttpParser& operator=(const HttpParser&) = delete;
    HttpParser& operator=(HttpParser&&) = delete;
    ~HttpParser() = default;
public:
    static HttpParser& get_fsm() {
        static HttpParser fsm;
        return fsm;
    };

    // 根据状态和处理函数，注册新的状态机事件
    HttpHandleCode register_fsm_handler(const FSMState&, const StateHandler&);
    // 执行Http解析状态机
    ParseResult fsm_excute(RequestHandlerPacket&);
};