#include "HttpContext.h"
#include "HttpModule.h"
#include "Logger.h"
#include "ServerPub.h"
#include <cstddef>

HttpContext::HttpContext(std::shared_ptr<RequestBuffer<char>> buffer,
    std::function<void(const size_t&, std::shared_ptr<RequestContent>&)> callback)
    : next_response_id_(1),
        next_request_id_(1),
        request_packet_(buffer),
        request_ready_callback_(callback) {}

// Implementation for parsing HTTP input
ParseResult HttpContext::HttpParseInput()
{
    ParseResult parse_ret;
    // 执行http状态机，数据解析完的数据保存在RequestHandlerPacket::content_buffer_中
    while ((parse_ret = HttpFsmManager::get_fsm().fsm_excute(request_packet_)) == ParseResult::COMPLETE) {
        auto parsed_content = request_packet_.pop_content();
        request_ready_callback_(next_request_id_, parsed_content);
        ++next_request_id_;
    }
    
    // 错包场景处理
    if (parse_ret == ParseResult::ERROR) {
        auto parsed_content = request_packet_.pop_content();
        parsed_content->method = HttpConst::METHOD_GET;
        parsed_content->url = HttpConst::PATH_ERR;
        parsed_content->keep_alive = false;
        request_ready_callback_(next_request_id_, parsed_content);
        ++next_request_id_;
    }
    return ParseResult::COMPLETE;
}

EVENT_STATUS HttpContext::push_response_in_queue(std::shared_ptr<OutgoingResponse>& response)
{
    if (this->response_map_.find(response->response_id_) != this->response_map_.end()) {
        LOG_ERR("Response with id [%zu] already exists in response queue.", response->response_id_);
        return EVENT_STATUS::RESPONSE_TASK_REPEATED;
    }
    response_map_[response->response_id_] = response;
    return EVENT_STATUS::OK;
}

std::shared_ptr<OutgoingResponse> HttpContext::get_response_in_queue()
{
    if (response_map_.find(next_response_id_) == response_map_.end()) {
        LOG_INFO("Next response with id [%zu] not found in response queue.", next_response_id_);
        return nullptr;
    }
    return response_map_[next_response_id_];
}

EVENT_STATUS HttpContext::update_response_queue()
{
    if (response_map_.find(next_response_id_) == response_map_.end()) {
        LOG_INFO("Next response with id [%zu] not existed, cannot pop!", next_response_id_);
        return EVENT_STATUS::RESPONSE_TASK_UNEXISTED;
    }
    if (response_map_.at(next_response_id_)->written_bytes_ ==
        response_map_.at(next_response_id_)->pending_write_bytes_) {
        return pop_response_in_queue();
    }
    return EVENT_STATUS::OK;
}

EVENT_STATUS HttpContext::pop_response_in_queue()
{
    if (response_map_.find(next_response_id_) == response_map_.end()) {
        LOG_INFO("Next response with id [%zu] not existed, cannot pop!", next_response_id_);
        return EVENT_STATUS::RESPONSE_TASK_UNEXISTED;
    }
    response_map_.erase(next_response_id_);
    ++next_response_id_;
    return EVENT_STATUS::OK;
}

bool HttpContext::all_response_done() const
{
    return next_response_id_ == next_request_id_ && response_map_.empty();
}