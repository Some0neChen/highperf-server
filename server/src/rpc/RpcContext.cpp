#include "RpcContext.h"
#include "Buffer.h"
#include "Logger.h"
#include "OutputChunk.h"
#include "RpcPub.h"
#include "RpcServer.h"
#include "TCPConnection.h"
#include <memory>
#include <utility>
#include "RpcProtocol.h"
#include "RpcParser.h"
#include "RpcService.h"

RpcContext::RpcContext()
    : request_id_(1), next_written_chunk_id_(1) {}

void RpcContext::set_dispatch_request_handle(std::function<void(std::function<void()>)> callback)
{
    dispatch_request_handle = std::move(callback);
}

void RpcContext::set_conn(std::weak_ptr<TCPConnection> conn)
{
    conn_ = conn;
}

bool RpcContext::parse_able(std::shared_ptr<Buffer<char>> buffer)
{
    if (buffer->readable_size() < RPC_HEADER_SPEC::HEADER_SIZE) {
        return false;
    }
    if (buffer->readable_size() - RPC_HEADER_SPEC::HEADER_SIZE < get_payload_len(buffer->get_data() + buffer->get_read_pos())) {
        return false;
    }
    return true;
}

void RpcContext::ParseInput(std::shared_ptr<Buffer<char>> buffer)
{
    // 执行Rpc解析，将TCP数据转换为TLV数据格式
    while (this->parse_able(buffer)) {
        auto data = parse_rpc_buffer(buffer);
        auto seq = this->request_id_;
        RpcServerManager::get_manager().excute_service_request(data, [this, seq](std::shared_ptr<OutputChunk> output) {
            this->save_respond_chunk(output, seq);
        });
    }
    // TODO 异常TLV报文处理...
}

void RpcContext::save_respond_chunk(std::shared_ptr<OutputChunk> chunk, const size_t& request_id)
{
    if (chunk == nullptr) {
        LOG_ERR("Get req[%u] response failed.", request_id);
        return;
    }

    auto conn_shared = conn_.lock();
    if (!conn_shared) {
        LOG_ERR("TCPConnection has failed.");
        return;
    }
    auto self_weak = weak_from_this();

    conn_shared->run_in_loop([conn_shared, chunk, request_id, self_weak]() {
        auto self_shared = self_weak.lock();
        if (!self_shared) {
            return;
        }
        self_shared->respond_chunks_.emplace(request_id, chunk);
        auto chunk_it = self_shared->respond_chunks_.begin();
        while ((chunk_it = self_shared->respond_chunks_.find(self_shared->next_written_chunk_id_)) != self_shared->respond_chunks_.end()) {
            conn_shared->send(chunk_it->second);
            self_shared->respond_chunks_.erase(chunk_it);
            ++self_shared->next_written_chunk_id_;
        }
        // RPC上层业务一般不涉及keep-alive，都是持久化连接。将来如果需要支持服务端自主断联型业务，可以在TLV的reserved取字段进行设置。
    });
}