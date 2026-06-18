#include "../../include/http/HttpContext.h"
#include "../../include/base/Buffer.h"
#include "../../include/http/HttpParser.h"
#include "HttpRouter.h"
#include "Logger.h"
#include "TCPConnection.h"
#include <memory>
#include <utility>

HttpContext::HttpContext()
    : request_id_(1), next_written_chunk_id_(1) {}

void HttpContext::set_dispatch_request_handle(std::function<void(std::function<void()>)> callback)
{
    dispatch_request_handle = std::move(callback);
}

void HttpContext::set_conn(std::weak_ptr<TCPConnection> conn)
{
    conn_ = conn;
}

void HttpContext::ParseInput(std::shared_ptr<Buffer<char>> buffer)
{
    ParseResult parse_ret;
    if (!packet_) {
        packet_ = std::make_unique<RequestHandlerPacket>(buffer);
    }
    // 执行http状态机，数据解析完的数据保存在RequestHandlerPacket::content_buffer_中
    while ((parse_ret = HttpParser::get_fsm().fsm_excute(*packet_)) == ParseResult::COMPLETE) {
        auto parsed_content = packet_->pop_content();
        if (parsed_content == nullptr) {
            LOG_ERR("HttpContext parse http request failed. parse result is complete but content is nullptr.");
            break;
        }
        // LOG_INFO("Parse HttpRequest:\n"
        //         "method         : %s\r\n"
        //         "url            : %s\r\n"
        //         "version        : %s\r\n"
        //         "keep_alive     : %s\r\n"
        //         "content_length : %u\r\n",
        //         parsed_content->method.c_str(), parsed_content->url.c_str(), parsed_content->version.c_str(),
        //         parsed_content->keep_alive ? "ture" : "false", parsed_content->content_length);
        // for (auto e : parsed_content->headers) {
        //     LOG_INFO("headers        : %s[%s]", e.first.c_str(), e.second.c_str());
        // }
        // LOG_INFO("Body : \r\n%s", parsed_content->body.c_str());

        auto seq = request_id_;
        // 通过回调将解析完的http数据传递给上层处理，交由上层线程池处
        // 处理完后通过HttpContext::save_respond_chunk将结果保存到HttpContext里，并分发给loop线程IO写回
        // 这里用weak_ptr捕获this，而不是直接使用裸指针this，避免回调执行时HttpContext已经析构导致的悬空指针问题
        auto self_weak = weak_from_this();
        dispatch_request_handle([self_weak, parsed_content, seq]() {
            auto self_shared = self_weak.lock();
            if (!self_shared) {
                return;
            }
            self_shared->save_respond_chunk(parsed_content, seq);
        });
        ++request_id_;

    }
    
    // 错包场景处理
    if (parse_ret == ParseResult::ERROR) {
        auto parsed_content = packet_->pop_content();
        parsed_content->method = HttpConst::METHOD_GET;
        parsed_content->url = HttpConst::PATH_ERR;
        parsed_content->keep_alive = false;

        auto seq = request_id_;
        auto self_weak = weak_from_this();
        dispatch_request_handle([self_weak, parsed_content, seq]() {
            auto self_shared = self_weak.lock();
            if (!self_shared) {
                return;
            }
            self_shared->save_respond_chunk(parsed_content, seq);
        });
        ++request_id_;
    }
    return;
}

void HttpContext::commit()
{

}

void HttpContext::save_respond_chunk(std::shared_ptr<RequestContent> req, const size_t& request_id)
{
    auto chunk = HttpRouter::get_router().get_response(req);
    if (chunk == nullptr) {
        LOG_ERR("Get req[%u] response failed.", request_id);
        return;
    }

    auto conn_shared = conn_.lock();
    if (!conn_shared) {
        LOG_ERR("TCPConnection has failed.");
        return;
    }

    auto keep_alive = req->keep_alive;
    auto self_weak = weak_from_this();

    conn_shared->run_in_loop([conn_shared, chunk, request_id, keep_alive, self_weak]() {
        auto self_shared = self_weak.lock();
        if (!self_shared) {
            return;
        }
        if (self_shared->next_written_chunk_id_ > request_id) {
            // 说明前面已有超时包裹返回，此处不接收
            return;
        }
        self_shared->respond_chunks_.emplace(request_id, chunk);
        auto chunk_it = self_shared->respond_chunks_.begin();
        while ((chunk_it = self_shared->respond_chunks_.find(self_shared->next_written_chunk_id_)) != self_shared->respond_chunks_.end()) {
            conn_shared->send(chunk_it->second);
            self_shared->respond_chunks_.erase(chunk_it);
            ++self_shared->next_written_chunk_id_;
        }
        if (!keep_alive) {
            conn_shared->shutdown();
        }
    });
}