#pragma once

#include "Logger.h"
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "RequestBuffer.h"

// 状态机挂接函数
void init_http_parse_fsm();

namespace HttpConst {
    // 分隔符
    constexpr std::string_view CRLF       = "\r\n";
    constexpr std::string_view HEADER_END = "\r\n\r\n";
    constexpr std::string_view SP         = " ";
    constexpr std::string_view COLON_SP   = ": ";

    // 请求方法
    constexpr std::string_view METHOD_GET  = "GET";
    constexpr std::string_view METHOD_POST = "POST";

    // 版本
    constexpr std::string_view VERSION_11 = "HTTP/1.1";

    // 请求头 key（统一小写，解析时 key 转小写后比较）
    constexpr std::string_view HEADER_CONTENT_LENGTH = "content-length";
    constexpr std::string_view HEADER_CONTENT_TYPE   = "content-type";
    constexpr std::string_view HEADER_CONNECTION      = "connection";
    constexpr std::string_view HEADER_HOST            = "host";

    // Connection 头的值
    constexpr std::string_view CONN_KEEP_ALIVE = "keep-alive";
    constexpr std::string_view CONN_CLOSE      = "close";

    // 常用路径
    constexpr std::string_view PATH_PING = "/ping";
    constexpr std::string_view PATH_ECHO = "/echo";

    // 响应状态
    constexpr std::string_view STATUS_200 = "200 OK";
    constexpr std::string_view STATUS_400 = "400 Bad Request";
    constexpr std::string_view STATUS_404 = "404 Not Found";
    constexpr std::string_view STATUS_500 = "500 Internal Server Error";

    // Content-Type 值
    constexpr std::string_view CONTENT_TYPE_TEXT = "text/plain";
    constexpr std::string_view CONTENT_TYPE_JSON = "application/json";
    constexpr std::string_view CONTENT_TYPE_HTML = "text/html";
}

namespace HttpTokens {
    constexpr uint16_t kLineEnd     = 0x0A0D;           // "\r\n"
    constexpr uint32_t kHeaderEnd   = 0x0A0D0A0D;       // "\r\n\r\n"
    constexpr uint16_t kHeaderSep   = 0x203A;           // ": "
}

enum class ParseState {
    REQUEST_LINE,
    HEADERS,
    BODY,
    COMPLETE,
    ERROR
};

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

enum class FSMResultCode {
    OK = 0,
    REGISER_HANDLER_ERR,
    END
};

struct RequestContent {
    std::string method;
    std::string url;
    std::string version;
    bool keep_alive;
    size_t content_length;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

class RequestHandlerPacket {
public:
    std::shared_ptr<RequestBuffer<char>> data_buffer_;
    std::shared_ptr<RequestContent> content_buffer_;
    FSMState current_state_;

    RequestHandlerPacket(std::shared_ptr<RequestBuffer<char>>);
    RequestHandlerPacket(RequestHandlerPacket&&) = default;
    RequestHandlerPacket(const RequestHandlerPacket&) = default;
    RequestHandlerPacket& operator=(RequestHandlerPacket&&) = default;
    RequestHandlerPacket& operator=(const RequestHandlerPacket&) = default;
    ~RequestHandlerPacket() = default;

    std::shared_ptr<RequestContent> pop_content();
};

class HttpFsmManager {
    using StateHandler = std::function<ParseResult(RequestHandlerPacket&)>;
    std::vector<StateHandler> state_handlers_;

    HttpFsmManager() = default;
    HttpFsmManager(const HttpFsmManager&) = delete;
    HttpFsmManager(HttpFsmManager&&) = delete;
    HttpFsmManager& operator=(const HttpFsmManager&) = delete;
    HttpFsmManager& operator=(HttpFsmManager&&) = delete;
    ~HttpFsmManager() = default;
public:
    static HttpFsmManager& get_fsm() {
        static HttpFsmManager fsm;
        return fsm;
    }

    FSMResultCode register_fsm_handler(const FSMState& state, const StateHandler& hander) {
        if ((state_handlers_.size()) != static_cast<decltype(state_handlers_.size())>(state)) {
            LOG_ERR("FSM state [%d] register handler error. current size [%zu]",
                    static_cast<decltype(state_handlers_.size())>(state), state_handlers_.size());
            return FSMResultCode::REGISER_HANDLER_ERR;
        }
        state_handlers_.push_back(hander);
        return FSMResultCode::OK;
    }

    ParseResult fsm_excute(RequestHandlerPacket& packet) {
        LOG_INFO("Http Parse FSM Excute.");
        auto paser_res = ParseResult::INCOMPLETE;
        while (packet.current_state_ < FSMState::END) {
            paser_res = state_handlers_[static_cast<decltype(state_handlers_.size())>(packet.current_state_)](packet);
            if (paser_res != ParseResult::COMPLETE) {
                return paser_res;
            }
        }
        if (packet.current_state_ > FSMState::END) {
            LOG_ERR("Undefined FSM state [%d] excute. current size [%zu]",
                static_cast<decltype(state_handlers_.size())>(packet.current_state_), state_handlers_.size());
            return ParseResult::ERROR;
        }
        packet.current_state_ = FSMState::START;
        LOG_INFO("Http Parse FSM Over.");
        return ParseResult::COMPLETE;
    }
};
