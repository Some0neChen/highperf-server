#include "HttpRouter.h"
#include "HttpPub.h"
#include "OutputChunk.h"
#include "Logger.h"
#include <deque>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// 根据路由类型返回对应的发送器，找不到则返回404报文
std::shared_ptr<OutputChunk> HttpRouter::get_response(std::shared_ptr<RequestContent>& req) {
    std::string route_key;
    route_key.append(req->method).append(HttpConst::SP).append(req->url);
    if (this->router_.find(route_key) != this->router_.end()) {
        return this->router_[route_key]->contructRespTaskPack(req);
    }
    if (this->router_.find(req->method) != this->router_.end()) {
        return this->router_[req->method]->contructRespTaskPack(req);
    }
    return this->router_[HttpRespond::NOT_FOUND]->contructRespTaskPack(req);
}

std::shared_ptr<OutputChunk> HttpRouter::get_unfound_response(std::shared_ptr<RequestContent>& req)
{
    return this->router_[HttpRespond::NOT_FOUND]->contructRespTaskPack(req);
}

std::shared_ptr<OutputChunk> HttpRouter::get_badrequest_response(std::shared_ptr<RequestContent>& req)
{
    return this->router_[HttpRespond::PARSE_FAULT]->contructRespTaskPack(req);
}

HttpHandleCode HttpRouter::register_http_sender(const std::string_view& respond_type, std::unique_ptr<HttpSender> sender)
{
    if (this->router_.find(respond_type) != this->router_.end()) {
        return HttpHandleCode::RESPOND_TYPE_REPEATED;
    }
    this->router_.emplace(respond_type, std::move(sender));
    return HttpHandleCode::OK;
}

// Http根据路由构造响应报文

// 公共函数
void append_common_headers(std::shared_ptr<OutputChunk> chunk, const StringResponseSpec& rsp_spec)
{
    // 配置报文头版本及返回码
    chunk->append(rsp_spec.version_).append(HttpConst::SP)
        .append(rsp_spec.status_).append(HttpConst::CRLF);
    // 配置返回类型
    chunk->append(HttpConst::HEADER_CONTENT_TYPE).append(HttpConst::COLON_SP)
        .append(rsp_spec.content_type_).append(HttpConst::CRLF);
    // 配置返回长度
    chunk->append(HttpConst::HEADER_CONTENT_LENGTH).append(HttpConst::COLON_SP)
        .append(std::to_string(rsp_spec.body_len_)).append(HttpConst::CRLF);
    // 设定是否为keep-alive
    chunk->append(HttpConst::HEADER_CONNECTION).append(HttpConst::COLON_SP);
    if (rsp_spec.keep_alive_) {
        chunk->append(HttpConst::CONN_KEEP_ALIVE);
    } else {
        chunk->append(HttpConst::CONN_CLOSE);
    }
    // 报文头空行/r/n/r/n, 代表报文头结束, 后续配置body
    chunk->append(HttpConst::CRLF).append(HttpConst::CRLF);
    
    return;
}

// 默认基类：对应当前处理方式未适配的情况
std::shared_ptr<OutputChunk> HttpSender::contructRespTaskPack(std::shared_ptr<RequestContent>& req) {
    std::shared_ptr<StringChunk> response = std::make_shared<StringChunk>();
    StringResponseSpec rsp_spec = {};
    // 配置报文头
    rsp_spec.version_ = HttpConst::VERSION_11;
    rsp_spec.status_ = HttpConst::STATUS_404;
    rsp_spec.content_type_ = HttpConst::CONTENT_TYPE_HTML;
    rsp_spec.body_len_ = HttpConst::MSG_404.size();
    rsp_spec.keep_alive_ = req->keep_alive;
    append_common_headers(response, rsp_spec);
    // 配置body
    response->append(HttpConst::MSG_404);
    
    return std::move(response);
}

// ping
// 对应为ping情况
std::shared_ptr<OutputChunk> HttpPingSender::contructRespTaskPack(std::shared_ptr<RequestContent>& req)
{
    std::shared_ptr<StringChunk> response = std::make_shared<StringChunk>();
    StringResponseSpec rsp_spec = {};
    // 配置报文头
    rsp_spec.version_ = HttpConst::VERSION_11;
    rsp_spec.status_ = HttpConst::STATUS_200;
    rsp_spec.content_type_ = HttpConst::CONTENT_TYPE_HTML;
    rsp_spec.body_len_ = HttpConst::MSG_PONG.size();
    rsp_spec.keep_alive_ = req->keep_alive;
    append_common_headers(response, rsp_spec);
    // 配置body
    response->append(HttpConst::MSG_PONG);

    return std::move(response);
}

// echo
// 对应为Post/echo情况
std::shared_ptr<OutputChunk> HttpEchoSender::contructRespTaskPack(std::shared_ptr<RequestContent>& req)
{
    std::shared_ptr<StringChunk> response = std::make_shared<StringChunk>();
    StringResponseSpec rsp_spec = {};
    // 配置报文头
    rsp_spec.version_ = HttpConst::VERSION_11;
    rsp_spec.status_ = HttpConst::STATUS_200;
    rsp_spec.content_type_ = HttpConst::CONTENT_TYPE_JSON;
    rsp_spec.body_len_ = req->body.size();
    rsp_spec.keep_alive_ = req->keep_alive;
    append_common_headers(response, rsp_spec);
    // 配置body
    response->append(std::move(req->body));

    return std::move(response);
}

// Fault
// 对应为400 报文解析出错的情况
std::shared_ptr<OutputChunk> HttpFaultSender::contructRespTaskPack(std::shared_ptr<RequestContent>& req)
{
    std::shared_ptr<StringChunk> response = std::make_shared<StringChunk>();
    std::string body = "Bad Request";
    StringResponseSpec rsp_spec = {};
    // 配置报文头
    rsp_spec.version_ = HttpConst::VERSION_11;
    rsp_spec.status_ = HttpConst::STATUS_400;
    rsp_spec.content_type_ = HttpConst::CONTENT_TYPE_TEXT;
    rsp_spec.body_len_ = body.size();
    // 400直接关闭连接
    rsp_spec.keep_alive_ = false;
    append_common_headers(response, rsp_spec);
    // 配置body
    response->append(std::move(body));

    return std::move(response);
}

// GET方法获取文件对应的返回类型
std::unordered_map<std::string, std::string> HttpGETFileSender::respond_type_map_ = {
    {".html", "text/html"},
    {".jpg",  "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".png",  "image/png"},
    {".gif",  "image/gif"},
    {".css",  "text/css"},
    {".js",   "application/javascript"}
};

// 根据URL获取返回类型
std::string HttpGETFileSender::getRespondType(std::shared_ptr<RequestContent>& req) const
{
    auto idx = req->url.find_last_of('.');
    if (idx == std::string::npos) {
        return "";
    }
    std::string type(req->url.c_str() + idx);
    if (this->respond_type_map_.find(type) == this->respond_type_map_.end()) {
        return "";
    }
    return this->respond_type_map_.at(type);
}

// 获取文件并存放到报文头中
void HttpGETFileSender::constructGetRespHeader(std::shared_ptr<OutputChunk> response,
    std::shared_ptr<RequestContent> req, std::string& type, const size_t& file_size)
{
    StringResponseSpec rsp_spec = {};
    // 配置报文头
    rsp_spec.version_ = HttpConst::VERSION_11;
    rsp_spec.status_ = HttpConst::STATUS_200;
    rsp_spec.content_type_ = type;
    rsp_spec.body_len_ = file_size;
    rsp_spec.keep_alive_ = req->keep_alive;
    append_common_headers(response, rsp_spec);
    return;
}

bool validate_static_path(std::string& path) {
    static constexpr std::string_view EMPTY = "";
    static constexpr std::string_view SLASH = "/";
    static constexpr std::string_view DOT = ".";
    static constexpr std::string_view DOT_DOT = "..";
    path.append(SLASH);
    size_t lhs_pos = 0;
    size_t rhs_pos = 0;
    std::deque<std::string_view> path_stack;
    while ((rhs_pos = path.find(SLASH, lhs_pos)) != std::string::npos) {
        path_stack.push_back(std::string_view(path.data() + lhs_pos, rhs_pos - lhs_pos));
        lhs_pos = rhs_pos + 1;
        if (path_stack.back() == EMPTY ||
            path_stack.back() == DOT ||
            path_stack.back() == SLASH) {
            path_stack.pop_back();
            continue;
        }
        if (path_stack.back() == DOT_DOT) {
            path_stack.pop_back();
            if (path_stack.empty()) {
                return false;
            }
            path_stack.pop_back();
        }
    }
    std::string valid_path("");
    while (!path_stack.empty()) {
        valid_path.append(SLASH).append(path_stack.front());
        path_stack.pop_front();
    }
    if (valid_path.empty()) {
        return false;
    }
    path = std::move(valid_path);
    return true;
}

std::shared_ptr<OutputChunk> HttpGETFileSender::contructRespTaskPack(std::shared_ptr<RequestContent>& req)
{
    std::shared_ptr<MMapFileChunk> response = std::make_shared<MMapFileChunk>();

    auto type = this->getRespondType(req);
    if (type == "") {
        return HttpRouter::get_router().get_unfound_response(req);
    }

    if (!validate_static_path(req->url)) {
        return HttpRouter::get_router().get_badrequest_response(req);
    }
    std::string file_path(HTTPROUTER_SPEC::SRC_PATH);
    file_path.append(req->url);
    auto file_fd = open(file_path.c_str(), O_RDONLY);
    if (file_fd == -1) {
        LOG_ERR("Http read path[%s] err. errno[%d] err reason:%s.",
                file_path.c_str(), errno, strerror(errno));
        return HttpRouter::get_router().get_unfound_response(req);
    }

    struct stat st;
    auto ret = fstat(file_fd, &st);
    if (ret == -1) {
        LOG_ERR("Http mmap file[%s] get size err. errno[%d] err reason:%s.",
            file_path.c_str(), errno, strerror(errno));
        close(file_fd);
        // TODO 这里应该返回500
        return HttpRouter::get_router().get_unfound_response(req);
    }
    size_t file_size = st.st_size;
    constructGetRespHeader(response, req, type, file_size);

    // 将文件数据映射到mmap区
    auto addr = mmap(nullptr, file_size,
        PROT_READ, MAP_PRIVATE, file_fd, 0);
    close(file_fd);
    if (addr == MAP_FAILED) {
        LOG_ERR("Http mmap file[%s] err. errno[%d] err reason:%s.",
                file_path.c_str(), errno, strerror(errno));
        return HttpRouter::get_router().get_unfound_response(req);
    }

    // 将报文头和mmap映射串到ivoec中
    response->set_chunk(file_size, addr);
    return response;
}

// 集中注册路由
void init_http_response_constructor()
{
    HttpRouter::get_router().register_http_sender(HttpRespond::NOT_FOUND,   std::make_unique<HttpSender>());
    HttpRouter::get_router().register_http_sender(HttpRespond::GET_PING,    std::make_unique<HttpPingSender>());
    HttpRouter::get_router().register_http_sender(HttpRespond::POST_PING,   std::make_unique<HttpEchoSender>());
    HttpRouter::get_router().register_http_sender(HttpRespond::PARSE_FAULT, std::make_unique<HttpFaultSender>());
    HttpRouter::get_router().register_http_sender(HttpConst::METHOD_GET,    std::make_unique<HttpGETFileSender>());
}