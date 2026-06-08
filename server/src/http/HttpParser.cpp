#include "HttpParser.h"
#include "Logger.h"
#include "Buffer.h"

HttpHandleCode HttpParser::register_fsm_handler(const FSMState& state, const StateHandler& handler)
{
    if ((state_handlers_.size()) != static_cast<decltype(state_handlers_.size())>(state)) {
        LOG_ERR("FSM state [%d] register handler error. current size [%zu]",
                static_cast<decltype(state_handlers_.size())>(state), state_handlers_.size());
        return HttpHandleCode::REGISER_HANDLER_ERR;
    }
    state_handlers_.push_back(handler);
    return HttpHandleCode::OK;
}

ParseResult HttpParser::fsm_excute(RequestHandlerPacket& packet) {
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

RequestHandlerPacket::RequestHandlerPacket(std::shared_ptr<Buffer<char>> buffer)
    : data_buffer_(buffer), content_buffer_(std::make_shared<RequestContent>()), current_state_(FSMState::START) {};

std::shared_ptr<RequestContent> RequestHandlerPacket::pop_content() {
    auto request_header = this->content_buffer_;
    this->content_buffer_ = std::make_shared<RequestContent>();
    return request_header;
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
    if (key == HttpConst::HEADER_CONNECTION) {
        if (value == HttpConst::CONN_CLOSE) {
            request.content_buffer_->keep_alive = false;
        } else {
            request.content_buffer_->keep_alive = true;
        }
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
    HttpParser::get_fsm().register_fsm_handler(FSMState::START,     &http_parse_request_start);
    HttpParser::get_fsm().register_fsm_handler(FSMState::METHOD,    &http_parse_request_method);
    HttpParser::get_fsm().register_fsm_handler(FSMState::PATH,      &http_parse_request_url);
    HttpParser::get_fsm().register_fsm_handler(FSMState::VERSION,   &http_parse_request_version);
    HttpParser::get_fsm().register_fsm_handler(FSMState::HEADER,    &http_parse_request_header);
    HttpParser::get_fsm().register_fsm_handler(FSMState::BODY,      &http_parse_request_body);
    return;
}