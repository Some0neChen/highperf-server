#include "HttpModule.h"
#include "HttpContext.h"
#include "Logger.h"
#include "ServerPub.h"
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <memory>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#include <unordered_map>
#include <sys/mman.h>
#include <utility>
#include "EventHandler.h"

// 结构体成员函数实现

HttpHandleCode HttpFsmManager::register_fsm_handler(const FSMState& state, const StateHandler& hander)
{
    if ((state_handlers_.size()) != static_cast<decltype(state_handlers_.size())>(state)) {
        LOG_ERR("FSM state [%d] register handler error. current size [%zu]",
                static_cast<decltype(state_handlers_.size())>(state), state_handlers_.size());
        return HttpHandleCode::REGISER_HANDLER_ERR;
    }
    state_handlers_.push_back(hander);
    return HttpHandleCode::OK;
}

ParseResult HttpFsmManager::fsm_excute(RequestHandlerPacket& packet)
{
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

RequestHandlerPacket::RequestHandlerPacket(std::shared_ptr<RequestBuffer<char>> buffer)
    : data_buffer_(buffer), content_buffer_(std::make_shared<RequestContent>()), current_state_(FSMState::START) {};

std::shared_ptr<RequestContent> RequestHandlerPacket::pop_content() {
    auto request_header = this->content_buffer_;
    this->content_buffer_ = std::make_shared<RequestContent>();
    return request_header;
}

// 将报文头中key的字段全部转为小写
void key_to_lower(char* begin, char* end)
{
    while (begin != end) {
        if (*begin >= 'A' && *begin <= 'Z') {
            *begin = 'a' + (*begin - 'A');
        }
        ++begin;
    }
}

// 4字节比较，提升性能
inline bool match_4_bytes(const char* data, const uint32_t& value)
{
    uint32_t data_val;
    memcpy(&data_val, data, 4);
    return data_val == value;
}

// 2字节比较，提升性能
inline bool match_2_bytes(const char* data, const uint16_t& value)
{
    uint16_t data_val;
    memcpy(&data_val, data, 2);
    return data_val == value;
}

// 更新Buffer和http解析任务包里的读指针
// request: http解析任务包里的读指针
// new_pos: 字符所在的位置
// token_size: 要跳过的字符大小
void update_read_pos(RequestHandlerPacket& request, size_t& new_pos, const size_t& token_size)
{
    new_pos += token_size; // 间隔符
    request.data_buffer_->update_read_pos(new_pos - request.data_buffer_->get_read_pos());
}

// 切分出下一个[空格]前的字符串
// request: http解析任务包
// pos: 最终修改为定位到的空格符所在索引位置
ParseResult get_content_upto_space(RequestHandlerPacket& request, size_t& pos)
{
    while (pos < request.data_buffer_->get_write_pos() && request.data_buffer_->get_data()[pos] != ' ') {
        ++pos;
    }
    // 如果没有找到空格，说明数据不完整，等下次再解析
    if (pos == request.data_buffer_->get_write_pos()) {
        return ParseResult::INCOMPLETE;
    }
    // 解析成功
    return ParseResult::COMPLETE;
}

// 切分出下一个[\r\n]前的字符串
// request: http解析任务包
// pos: 最终修改为定位到的\r\n所在索引位置
ParseResult get_content_upto_crlf(RequestHandlerPacket& request, size_t& pos)
{
    while (pos + 1 < request.data_buffer_->get_write_pos()
        && !match_2_bytes(request.data_buffer_->get_data() + pos, HttpTokens::kLineEnd)) {
        ++pos;
    }
    // 如果没有找到行结束符\r\n，说明数据不完整，等下次再解析
    if (pos + 1 >= request.data_buffer_->get_write_pos()) {
        return ParseResult::INCOMPLETE;
    }
    // 解析成功
    return ParseResult::COMPLETE;
}

// 切分出下一个[: ]前的字符串
// request: http解析任务包
// pos: 最终修改为定位到的[: ]所在索引位置
ParseResult get_content_upto_colon_sp(RequestHandlerPacket& request, size_t& pos)
{
    while (pos + 2 < request.data_buffer_->get_write_pos()
        && !match_2_bytes(request.data_buffer_->get_data() + pos, HttpTokens::kHeaderSep)) {
        ++pos;
    }
    // 如果没有找到行冒号分割符[: ]，说明数据不完整，等下次再解析
    if (pos + 1 >= request.data_buffer_->get_write_pos()) {
        return ParseResult::INCOMPLETE;
    }
    // 解析成功
    return ParseResult::COMPLETE;
}

// 报文解析状态机起始
ParseResult http_parse_request_start(RequestHandlerPacket& request)
{
    request.current_state_ = FSMState::METHOD;
    return ParseResult::COMPLETE;
}

// 解析请求方法
ParseResult http_parse_request_method(RequestHandlerPacket& request)
{
    size_t pos = request.data_buffer_->get_read_pos();
    if (get_content_upto_space(request, pos) != ParseResult::COMPLETE) {
        return ParseResult::INCOMPLETE;
    }
    request.content_buffer_->method = std::string(request.data_buffer_->get_data() + request.data_buffer_->get_read_pos(),
        pos - request.data_buffer_->get_read_pos());
    update_read_pos(request, pos, HttpConst::SP.size());
    request.current_state_ = FSMState::PATH;
    return ParseResult::COMPLETE;
}

// 解析请求路径
ParseResult http_parse_request_url(RequestHandlerPacket& request)
{
    size_t pos = request.data_buffer_->get_read_pos();
    if (get_content_upto_space(request, pos) != ParseResult::COMPLETE) {
        return ParseResult::INCOMPLETE;
    }
    request.content_buffer_->url = std::string(request.data_buffer_->get_data() + request.data_buffer_->get_read_pos(),
        pos - request.data_buffer_->get_read_pos());
    update_read_pos(request, pos, HttpConst::SP.size());
    request.current_state_ = FSMState::VERSION;
    return ParseResult::COMPLETE;
}

// 解析请求版本
ParseResult http_parse_request_version(RequestHandlerPacket& request)
{
    size_t pos = request.data_buffer_->get_read_pos();
    if (get_content_upto_crlf(request, pos) != ParseResult::COMPLETE) {
        return ParseResult::INCOMPLETE;
    }
    request.content_buffer_->version = std::string(request.data_buffer_->get_data() + request.data_buffer_->get_read_pos(),
        pos - request.data_buffer_->get_read_pos());
    if (request.content_buffer_->version == HttpConst::VERSION_11) {
        request.content_buffer_->keep_alive = true;
    }
    update_read_pos(request, pos, HttpConst::CRLF.size());
    request.current_state_ = FSMState::HEADER;
    return ParseResult::COMPLETE;
}

// 解析报文头键值对
ParseResult http_parse_request_header(RequestHandlerPacket& request)
{
    // 如果在读报文头阶段开头就读到\r\n，那说明已经进入了\r\n\r\n的情况，转而去读取BODY
    if (request.data_buffer_->get_read_pos() + 1 < request.data_buffer_->get_write_pos()
        && match_2_bytes(request.data_buffer_->get_data() + request.data_buffer_->get_read_pos(), HttpTokens::kLineEnd)) {
        request.current_state_ = FSMState::BODY;
        return ParseResult::COMPLETE;
    }

    // 定位到[: ]的起始索引
    size_t colon_sp_pos = request.data_buffer_->get_read_pos();
    if (get_content_upto_colon_sp(request, colon_sp_pos) != ParseResult::COMPLETE) {
        return ParseResult::INCOMPLETE;
    }
    // 跳过[: ]
    size_t crlf_pos = colon_sp_pos + HttpConst::COLON_SP.size();
    // 定位到[\r\n]的起始索引
    if (get_content_upto_crlf(request, crlf_pos) != ParseResult::COMPLETE) {
        return ParseResult::INCOMPLETE;
    }

    // 将键值对保存在请求任务包中
    key_to_lower(request.data_buffer_->get_data() + request.data_buffer_->get_read_pos(),
        request.data_buffer_->get_data() + colon_sp_pos - 1);
    std::string_view key(request.data_buffer_->get_data() + request.data_buffer_->get_read_pos(),
        colon_sp_pos - request.data_buffer_->get_read_pos());
    std::string_view value(request.data_buffer_->get_data() + colon_sp_pos + HttpConst::COLON_SP.size(),
        crlf_pos - colon_sp_pos - HttpConst::CRLF.size());

    // 存放请求头数据
    if (!request.content_buffer_->keep_alive && key == HttpConst::HEADER_CONNECTION
        && value == HttpConst::CONN_KEEP_ALIVE) {
        request.content_buffer_->keep_alive = true;
    } else if (key == HttpConst::HEADER_CONTENT_LENGTH) {
        try {
            request.content_buffer_->content_length = std::stoul(std::string(value));
        } catch (const std::exception& e) {
            LOG_ERR("Parse content-length err! Err reason: %s", e.what());
            return ParseResult::ERROR;
        }
    } else {
        request.content_buffer_->headers.emplace(key, value);
    }
    update_read_pos(request, crlf_pos, HttpConst::CRLF.size());
    // 继续走请求头状态机解析
    return ParseResult::COMPLETE;
}

// 判断报文是否结束以及如果是POST，解析对应报文里的Body部分
ParseResult http_parse_request_body(RequestHandlerPacket& request)
{
    // 此处需要吃掉开头的\r\n, 当前在header的处理最后并没有处理额外的\r\n
    auto pos = request.data_buffer_->get_read_pos();
    update_read_pos(request, pos, HttpConst::CRLF.size());

    // content_length代表无body数据携带，此时说明该http报文已解析完毕
    if (request.content_buffer_->content_length == 0) {
        request.current_state_ = FSMState::END;
        return ParseResult::COMPLETE;
    }

    // 当前缓冲区剩余大小小于content_length长度，说明正在读半包
    if (request.data_buffer_->get_read_pos() + request.content_buffer_->content_length > request.data_buffer_->get_write_pos()) {
        return ParseResult::INCOMPLETE;
    }

    // 当前http报文已解析完毕
    request.content_buffer_->body = std::string(request.data_buffer_->get_data() + request.data_buffer_->get_read_pos(),
        request.content_buffer_->content_length);
    auto next_start_pos = request.data_buffer_->get_read_pos() + request.content_buffer_->content_length;
    update_read_pos(request, next_start_pos, 0);
    request.current_state_ = FSMState::END;
    return ParseResult::COMPLETE;
}

// HTTP解析状态机函数注册
void init_http_parse_fsm()
{
    HttpFsmManager::get_fsm().register_fsm_handler(FSMState::START,     &http_parse_request_start);
    HttpFsmManager::get_fsm().register_fsm_handler(FSMState::METHOD,    &http_parse_request_method);
    HttpFsmManager::get_fsm().register_fsm_handler(FSMState::PATH,      &http_parse_request_url);
    HttpFsmManager::get_fsm().register_fsm_handler(FSMState::VERSION,   &http_parse_request_version);
    HttpFsmManager::get_fsm().register_fsm_handler(FSMState::HEADER,    &http_parse_request_header);
    HttpFsmManager::get_fsm().register_fsm_handler(FSMState::BODY,      &http_parse_request_body);
    return;
}

// HttpResponse报文构造API部分

// 根据路由类型返回对应的发送器，找不到则返回404报文
std::shared_ptr<OutgoingResponse> HttpRouter::get_response(std::shared_ptr<HttpRequestTask>& task) {
    std::string route_key;
    route_key.append(task->request_header_->method).append(HttpConst::SP).append(task->request_header_->url);
    if (this->router_.find(route_key) != this->router_.end()) {
        return this->router_[route_key]->contructRespTaskPack(task);
    }
    if (this->router_.find(task->request_header_->method) != this->router_.end()) {
        return this->router_[task->request_header_->method]->contructRespTaskPack(task);
    }
    return this->router_[HttpRespond::NOT_FOUND]->contructRespTaskPack(task);
}

std::shared_ptr<OutgoingResponse> HttpRouter::get_unfound_response(std::shared_ptr<HttpRequestTask>& task)
{
    return this->router_[HttpRespond::NOT_FOUND]->contructRespTaskPack(task);
}

HttpHandleCode HttpRouter::respond(std::shared_ptr<HttpRequestTask>& task)
{
    auto conn = task->connetion_.lock();
    if (!conn) {
        LOG_ERR("TCP Connection expired. Cant feedback respond[%d].",
            task->seq_no_);
        return HttpHandleCode::TCP_CLOSED;
    }

    auto response = this->get_response(task);
    if (response == nullptr) {
        // 当前逻辑不可能走入该分支，只做白盒级防范
        LOG_ERR("Client[%d] get req[%u] response failed.",
            conn->get_fd(), task->seq_no_);
        return HttpHandleCode::ERR;
    }

    auto conn_weak_ptr = task->connetion_;
    conn->queue_task_in_reactor([response, conn_weak_ptr]() mutable {
        auto tcp_conn = conn_weak_ptr.lock();
        if (!tcp_conn) {
            return EVENT_STATUS::CLIENT_EXPIRED;
        }
        tcp_conn->on_response_ready(response);
        return EVENT_STATUS::OK;
    });
    return HttpHandleCode::OK;
}

HttpHandleCode HttpRouter::regisetr_http_sender(const std::string_view& respond_type, std::unique_ptr<HttpSender> sender)
{
    if (this->router_.find(respond_type) != this->router_.end()) {
        return HttpHandleCode::RESPOND_TYPE_REPEATED;
    }
    this->router_.emplace(respond_type, std::move(sender));
    return HttpHandleCode::OK;
}

StringResponse::StringResponse(const size_t& no) : OutgoingResponse(no)
{ 
    response_data_.reserve(SPECS_VALUE::HTTP_RESPOND_MSG_SIZE);
}

StringResponse& StringResponse::append(std::string&& data)
{
    pending_write_bytes_ += data.size();
    response_data_.append(std::forward<std::string>(data));
    return *this;
}

StringResponse& StringResponse::append(const std::string_view& data)
{
    response_data_.append(data);
    pending_write_bytes_ += data.size();
    return *this;
}

TCPWriteResult StringResponse::wirteToSocket(const int& fd)
{
    ssize_t write_len = 0;
    while (true) {
        write_len = send(fd, response_data_.data() + written_bytes_,
            pending_write_bytes_ - written_bytes_, MSG_NOSIGNAL);

        if (write_len == -1) {
            if (errno == EINTR) {
                LOG_INFO("System interrupt listen TCP Connection[%d] to write [str]tcp response packet. errono[%d]", fd, errno);
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                LOG_INFO("TCP Connection[%d] trig sending block[str]. errono[%d]", fd, errno);
                break;
            }
            LOG_ERR("TCP Connection[%d] send back msg failed[str]. ret %d errno %d errmsg %s",
                fd, write_len, errno, strerror(errno));
            return TCPWriteResult::ERROR;
        }

        written_bytes_ += write_len;
        LOG_INFO("[Str]Client[%d] send back len [%d] msg success. Already sended response bytes [%u], "
            "Total [%u].", fd, write_len, written_bytes_, pending_write_bytes_);
        if (written_bytes_ == pending_write_bytes_) {
            return TCPWriteResult::COMPLETE;
        }
    }
    return TCPWriteResult::PARTIAL;
}

// 默认基类：对应当前处理方式未适配的情况
std::shared_ptr<OutgoingResponse> HttpSender::contructRespTaskPack(std::shared_ptr<HttpRequestTask>& task) {
    std::shared_ptr<StringResponse> response = std::make_shared<StringResponse>(task->seq_no_);
    
    // 配置报文头版本及返回码
    response->append(HttpConst::VERSION_11).append(HttpConst::SP)
        .append(HttpConst::STATUS_404).append(HttpConst::CRLF);
    // 配置返回类型
    response->append(HttpConst::HEADER_CONTENT_TYPE).append(HttpConst::COLON_SP)
        .append(HttpConst::CONTENT_TYPE_HTML).append(HttpConst::CRLF);
    // 配置返回长度
    response->append(HttpConst::HEADER_CONTENT_LENGTH).append(HttpConst::COLON_SP)
        .append(std::to_string(HttpConst::MSG_404.size())).append(HttpConst::CRLF);
    // 设定是否为keep-alive
    response->append(HttpConst::HEADER_CONNECTION).append(HttpConst::COLON_SP);
    response->keep_alive_ = task->request_header_->keep_alive;
    if (task->request_header_->keep_alive) {
        response->append(HttpConst::CONN_KEEP_ALIVE);
    } else {
        response->append(HttpConst::CONN_CLOSE);
    }
    response->append(HttpConst::CRLF);
    // 配置空行，代表报文头结束，要配置Body
    response->append(HttpConst::CRLF);
    response->append(HttpConst::MSG_404);
    
    return std::move(response);
}

// ping
// 对应为ping情况
std::shared_ptr<OutgoingResponse> HttpPingSender::contructRespTaskPack(std::shared_ptr<HttpRequestTask>& task)
{
    std::shared_ptr<StringResponse> response = std::make_shared<StringResponse>(task->seq_no_);

    // 配置报文头版本及返回码
    response->append(HttpConst::VERSION_11).append(HttpConst::SP)
        .append(HttpConst::STATUS_200).append(HttpConst::CRLF);
    // 配置返回类型
    response->append(HttpConst::HEADER_CONTENT_TYPE).append(HttpConst::COLON_SP)
        .append(HttpConst::CONTENT_TYPE_HTML).append(HttpConst::CRLF);
    // 配置返回长度
    response->append(HttpConst::HEADER_CONTENT_LENGTH).append(HttpConst::COLON_SP)
        .append(std::to_string(HttpConst::MSG_PONG.size())).append(HttpConst::CRLF);
    // 设定是否为keep-alive
    response->append(HttpConst::HEADER_CONNECTION).append(HttpConst::COLON_SP);
    response->keep_alive_ = task->request_header_->keep_alive;
    if (task->request_header_->keep_alive) {
        response->append(HttpConst::CONN_KEEP_ALIVE);
    } else {
        response->append(HttpConst::CONN_CLOSE);
    }
    response->append(HttpConst::CRLF);
    // 配置空行，代表报文头结束，要配置Body
    response->append(HttpConst::CRLF);
    response->append(HttpConst::MSG_PONG);

    return std::move(response);
}

// echo
// 对应为Post/echo情况
std::shared_ptr<OutgoingResponse> HttpEchoSender::contructRespTaskPack(std::shared_ptr<HttpRequestTask>& task)
{
    std::shared_ptr<StringResponse> response = std::make_shared<StringResponse>(task->seq_no_);
    std::string body = "{\"msg\":\"hello\"}";
    // 配置报文头版本及返回码
    response->append(HttpConst::VERSION_11).append(HttpConst::SP)
        .append(HttpConst::STATUS_200).append(HttpConst::CRLF);
    // 配置返回类型
    response->append(HttpConst::HEADER_CONTENT_TYPE).append(HttpConst::COLON_SP)
        .append(HttpConst::CONTENT_TYPE_JSON).append(HttpConst::CRLF);
    // 配置返回长度
    response->append(HttpConst::HEADER_CONTENT_LENGTH).append(HttpConst::COLON_SP)
        .append(std::to_string(body.size())).append(HttpConst::CRLF);
    // 设定是否为keep-alive
    response->append(HttpConst::HEADER_CONNECTION).append(HttpConst::COLON_SP);
    response->keep_alive_ = task->request_header_->keep_alive;
    if (task->request_header_->keep_alive) {
        response->append(HttpConst::CONN_KEEP_ALIVE);
    } else {
        response->append(HttpConst::CONN_CLOSE);
    }
    response->append(HttpConst::CRLF);
    // 配置空行，代表报文头结束，要配置Body
    response->append(HttpConst::CRLF);
    response->append(std::move(body));

    return std::move(response);
}

// Fault
// 对应为400 报文解析出错的情况
std::shared_ptr<OutgoingResponse> HttpFaultSender::contructRespTaskPack(std::shared_ptr<HttpRequestTask>& task)
{
    std::shared_ptr<StringResponse> response = std::make_shared<StringResponse>(task->seq_no_);
    std::string body = "Bad Request";
    // 配置报文头版本及返回码
    response->append(HttpConst::VERSION_11).append(HttpConst::SP)
        .append(HttpConst::STATUS_400).append(HttpConst::CRLF);
    // 配置返回类型
    response->append(HttpConst::HEADER_CONTENT_TYPE).append(HttpConst::COLON_SP)
        .append(HttpConst::CONTENT_TYPE_TEXT).append(HttpConst::CRLF);
    // 配置返回长度
    response->append(HttpConst::HEADER_CONTENT_LENGTH).append(HttpConst::COLON_SP)
        .append(std::to_string(body.size())).append(HttpConst::CRLF);
    // 400统一关掉连接，不维持Keep-Alive
    response->keep_alive_ = false;
    response->append(HttpConst::HEADER_CONNECTION).append(HttpConst::COLON_SP);
    response->append(HttpConst::CONN_CLOSE);
    response->append(HttpConst::CRLF);
    // 配置空行，代表报文头结束，要配置Body
    response->append(HttpConst::CRLF);
    response->append(std::move(body));

    return std::move(response);
}

MmapFileResponse& MmapFileResponse::append(const std::string_view& data)
{
    header_data_.append(data);
    heder_size_ += data.size();
    return *this;
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

const std::string HttpGETFileSender::SRC_PATH = "/home/some0nechen/文档/code/CPPServer/src";

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
void HttpGETFileSender::constructGetRespHeader(std::shared_ptr<MmapFileResponse>& response,
    std::shared_ptr<RequestContent>& req, std::string& type)
{
    // 配置报文头版本及返回码
    response->append(HttpConst::VERSION_11).append(HttpConst::SP)
        .append(HttpConst::STATUS_200).append(HttpConst::CRLF);
    // 配置返回类型
    response->append(HttpConst::HEADER_CONTENT_TYPE).append(HttpConst::COLON_SP)
        .append(std::move(type)).append(HttpConst::CRLF);
    // 配置返回长度
    response->append(HttpConst::HEADER_CONTENT_LENGTH).append(HttpConst::COLON_SP)
        .append(std::to_string(response->file_size_)).append(HttpConst::CRLF);
    // 设定是否为keep-alive
    response->keep_alive_ = req->keep_alive;
    response->append(HttpConst::HEADER_CONNECTION).append(HttpConst::COLON_SP);
    if (req->keep_alive) {
        response->append(HttpConst::CONN_KEEP_ALIVE);
    } else {
        response->append(HttpConst::CONN_CLOSE);
    }
    response->append(HttpConst::CRLF);
    // 配置空行，代表报文头结束，要配置Body
    response->append(HttpConst::CRLF);

    return;
}

std::shared_ptr<OutgoingResponse> HttpGETFileSender::contructRespTaskPack(std::shared_ptr<HttpRequestTask>& task)
{
    std::shared_ptr<MmapFileResponse> response = std::make_shared<MmapFileResponse>(task->seq_no_);

    auto type = this->getRespondType(task->request_header_);
    if (type == "") {
        return HttpRouter::get_router().get_unfound_response(task);
    }

    std::string file_path = this->SRC_PATH + task->request_header_->url;
    auto file_fd = open(file_path.c_str(), O_RDONLY);
    if (file_fd == -1) {
        LOG_ERR("Http read path[%s] err. errno[%d] err reason:%s.",
                file_path.c_str(), errno, strerror(errno));
        return HttpRouter::get_router().get_unfound_response(task);
    }

    struct stat st;
    auto ret = fstat(file_fd, &st);
    if (ret == -1) {
        LOG_ERR("Http mmap file[%s] get size err. errno[%d] err reason:%s.",
            file_path.c_str(), errno, strerror(errno));
        close(file_fd);
        // TODO 这里应该返回500
        return HttpRouter::get_router().get_unfound_response(task);
    }
    response->file_size_ = st.st_size;
    this->constructGetRespHeader(response, task->request_header_, type);

    // 将文件数据映射到mmap区
    auto addr = mmap(nullptr, response->file_size_,
        PROT_READ, MAP_PRIVATE, file_fd, 0);
    close(file_fd);
    if (addr == MAP_FAILED) {
        LOG_ERR("Http mmap file[%s] err. errno[%d] err reason:%s.",
                file_path.c_str(), errno, strerror(errno));
        return HttpRouter::get_router().get_unfound_response(task);
    }
    response->mmap_addr_ = addr;
    
    // 将报文头和mmap映射串到ivoec中
    response->iov_[0].iov_base = response->header_data_.data();
    response->iov_[0].iov_len = response->heder_size_;
    response->iov_[1].iov_base = addr;
    response->iov_[1].iov_len = response->file_size_;

    response->pending_write_bytes_ = response->heder_size_ + response->file_size_;

    return response;
}

TCPWriteResult MmapFileResponse::wirteToSocket(const int& fd)
{
    ssize_t write_len = 0;
    while (true) {
        write_len = writev(fd, iov_.data(), iov_.size());

        if (write_len == -1) {
            if (errno == EINTR) {
                LOG_INFO("System interrupt listen TCP Connection[%d] to write [mmap]tcp response packet. errono[%d]", fd, errno);
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                LOG_INFO("TCP Connection[%d] trig sending block[mmap]. errono[%d]", fd, errno);
                break;
            }
            LOG_ERR("TCP Connection[%d] send back msg failed[mmap]. ret %d errno %d errmsg %s",
                fd, write_len, errno, strerror(errno));
            return TCPWriteResult::ERROR;
        }

        written_bytes_ += write_len;
        LOG_INFO("MMAP Client[%d] send back len [%d] msg success. Already sended response bytes [%u], "
            "Total [%u].", fd, write_len, written_bytes_, pending_write_bytes_);
        if (pending_write_bytes_ == written_bytes_) {
            return TCPWriteResult::COMPLETE;
        }

        if (write_len >= iov_[0].iov_len) {
            write_len -= iov_[0].iov_len;
            iov_[0].iov_len = 0;
        } else {
            iov_[0].iov_len -= write_len;
            iov_[0].iov_base = static_cast<char*>(iov_[0].iov_base) + write_len;
            continue;
        }

        iov_[1].iov_base = (static_cast<char*>(iov_[1].iov_base) + write_len);
        iov_[1].iov_len -= write_len;
    }
    return TCPWriteResult::PARTIAL;
}

MmapFileResponse::~MmapFileResponse()
{
    if (this->mmap_addr_ && this->file_size_ > 0) {
        munmap(this->mmap_addr_, this->file_size_);
    }
}

// 集中注册路由
void HttpRouteAttach()
{
    HttpRouter::get_router().regisetr_http_sender(HttpRespond::NOT_FOUND,   std::make_unique<HttpSender>());
    HttpRouter::get_router().regisetr_http_sender(HttpRespond::GET_PING,    std::make_unique<HttpPingSender>());
    HttpRouter::get_router().regisetr_http_sender(HttpRespond::POST_PING,   std::make_unique<HttpEchoSender>());
    HttpRouter::get_router().regisetr_http_sender(HttpRespond::PARSE_FAULT, std::make_unique<HttpFaultSender>());
    HttpRouter::get_router().regisetr_http_sender(HttpConst::METHOD_GET,    std::make_unique<HttpGETFileSender>());
}