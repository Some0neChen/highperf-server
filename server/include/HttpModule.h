#pragma once

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
void HttpRouteAttach();

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

    // 响应信息
    constexpr std::string_view MSG_200  = "OK";
    constexpr std::string_view MSG_400  = "Bad Request";
    constexpr std::string_view MSG_404  = "Not Found";
    constexpr std::string_view MSG_500  = "Internal Server Error";
    constexpr std::string_view MSG_PONG = "pong";
}

namespace HttpRespond {
    // 未找到
    constexpr std::string_view NOT_FOUND = "HTTP/1.1 Not Found";
    // Parse Err
    constexpr std::string_view PARSE_FAULT = "PARSE_FAULT";
    // GET Ping
    constexpr std::string_view GET_PING = "GET /ping";
    // Post Echo
    constexpr std::string_view POST_PING = "POST /echo";
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

enum class HttpHandleCode {
    OK = 0,
    ERR,
    REGISER_HANDLER_ERR,
    RESPOND_TYPE_REPEATED,
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

    // 将解析完的http数据返回，并分配新的堆区用来存取下一个http数据
    std::shared_ptr<RequestContent> pop_content();
};

// Http解析状态机
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

    // 根据状态和处理函数，注册新的状态机事件
    HttpHandleCode register_fsm_handler(const FSMState&, const StateHandler&);
    // 执行Http解析状态机
    ParseResult fsm_excute(RequestHandlerPacket&);
};

class HttpSender;
// 路由规则处理器，根据报文的method和url进行处理，获取对应的body，以及返回状态码
class HttpRouter {
    // 通过sender函数，传入RequestContent，获取对应的处理类HttpSender
    std::unordered_map<std::string_view, std::shared_ptr<HttpSender>> router_;
    HttpRouter() = default;
    HttpRouter(const HttpRouter&) = delete;
    HttpRouter(HttpRouter&&) = delete;
    HttpRouter& operator=(const HttpRouter&) = delete;
    HttpRouter& operator=(HttpRouter&&) = delete;
    ~HttpRouter() = default;
    std::shared_ptr<HttpSender> default_sender_;
public:
    static HttpRouter& get_router() {
        static HttpRouter router;
        return router;
    }

    HttpHandleCode regisetr_http_sender(const std::string_view&, std::shared_ptr<HttpSender>);
    HttpHandleCode respond(std::shared_ptr<RequestContent>&, int);
};

// http回送报文构造器，基类为404情况
class HttpSender {
protected:
    HttpSender(const HttpSender&) = delete;
    HttpSender(HttpSender&&) = delete;
    HttpSender& operator=(const HttpSender&) = delete;
    HttpSender& operator=(HttpSender&&) = delete;
public:
    HttpSender() = default;
    ~HttpSender() = default;
    virtual HttpHandleCode constructMsg(std::string&, std::shared_ptr<RequestContent>&);
    virtual HttpHandleCode sendMsg(std::shared_ptr<RequestContent>&, int);
};

// 对应为GET/ping情况
class HttpPingSender : public HttpSender {
public:
    HttpPingSender() = default;
    HttpHandleCode constructMsg(std::string&, std::shared_ptr<RequestContent>&) override;
};

// 对应为Post/echo情况
class HttpEchoSender : public HttpSender {
public:
    HttpEchoSender() = default;
    HttpHandleCode constructMsg(std::string&, std::shared_ptr<RequestContent>&) override;
};

// 对应为400报文错误无法解析的情况
class HttpFaultSender : public HttpSender {
public:
    HttpFaultSender() = default;
    HttpHandleCode constructMsg(std::string&, std::shared_ptr<RequestContent>&) override;
};

// GET方法在未精确命中的情况下的广义文件搜寻
class HttpGETFileSender : public HttpSender {
private:
    static std::unordered_map<std::string, std::string> respond_type_map_;
    std::string getRespondType(std::shared_ptr<RequestContent>&) const;
public:
    HttpGETFileSender() = default;
    HttpHandleCode constructMsg(std::string&, std::shared_ptr<RequestContent>&) override;
};