#pragma once
#include <functional>
#include <unordered_map>
#include <string_view>
#include <memory>
#include "HttpPub.h"

void init_http_response_constructor();

namespace HTTPROUTER_SPEC {
    // 资源文件路径
    static constexpr std::string_view SRC_PATH = "/home/some0nechen/文档/code/CPPServer/src";
}

struct StringResponseSpec {
    std::string_view version_;
    std::string_view status_;
    std::string_view content_type_;
    size_t body_len_;
    bool keep_alive_;
};

class HttpSender;
class OutputChunk;
// 路由规则处理器，根据报文的method和url进行处理，获取对应的body，以及返回状态码
class HttpRouter {
    // 通过sender函数，传入RequestContent，获取对应的处理类HttpSender
    std::unordered_map<std::string_view, std::unique_ptr<HttpSender>> router_;
    HttpRouter() = default;
    HttpRouter(const HttpRouter&) = delete;
    HttpRouter(HttpRouter&&) = delete;
    HttpRouter& operator=(const HttpRouter&) = delete;
    HttpRouter& operator=(HttpRouter&&) = delete;
    ~HttpRouter() = default;
public:
    static HttpRouter& get_router() {
        static HttpRouter router;
        return router;
    }

    HttpHandleCode register_http_sender(const std::string_view&, std::unique_ptr<HttpSender>);
    void route(std::shared_ptr<RequestContent>&, std::function<void()>);
    std::shared_ptr<OutputChunk> get_response(std::shared_ptr<RequestContent>&);
    // 404
    std::shared_ptr<OutputChunk> get_unfound_response(std::shared_ptr<RequestContent>&);
    // 400
    std::shared_ptr<OutputChunk> get_badrequest_response(std::shared_ptr<RequestContent>&);
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
    virtual std::shared_ptr<OutputChunk> contructRespTaskPack(std::shared_ptr<RequestContent>&);
};

// 对应为GET/ping情况
class HttpPingSender : public HttpSender {
public:
    HttpPingSender() = default;
    std::shared_ptr<OutputChunk> contructRespTaskPack(std::shared_ptr<RequestContent>&) override;
};

// 对应为Post/echo情况
class HttpEchoSender : public HttpSender {
public:
    HttpEchoSender() = default;
    std::shared_ptr<OutputChunk> contructRespTaskPack(std::shared_ptr<RequestContent>&) override;
};

// 对应为400报文错误无法解析的情况
class HttpFaultSender : public HttpSender {
public:
    HttpFaultSender() = default;
    std::shared_ptr<OutputChunk> contructRespTaskPack(std::shared_ptr<RequestContent>&) override;
};

class MMapFileChunk;
// GET方法在未精确命中的情况下的广义文件搜寻
class HttpGETFileSender : public HttpSender {
    static std::unordered_map<std::string, std::string> respond_type_map_;
    std::string getRespondType(std::shared_ptr<RequestContent>&) const;
    void constructGetRespHeader(std::shared_ptr<OutputChunk>,
        std::shared_ptr<RequestContent>, std::string&, const size_t&);
public:
    HttpGETFileSender() = default;
    std::shared_ptr<OutputChunk> contructRespTaskPack(std::shared_ptr<RequestContent>&) override;
};