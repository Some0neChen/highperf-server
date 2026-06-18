#include "RpcServer.h"
#include "RpcContext.h"
#include <memory>

void RpcServer::set_rpc_context(std::shared_ptr<TCPConnection> tcp_conn)
{
    auto rpc_context = std::make_shared<RpcContext>();
    rpc_context->set_dispatch_request_handle([this](std::function<void()> handle) {
        this->tpool_.add_task(handle);
    });
    tcp_conn->set_context(rpc_context);
    rpc_context->set_conn(tcp_conn);
    return;
}

void RpcServer::read_tcp_buffer(std::weak_ptr<TCPConnection> conn)
{
    auto conn_entity = conn.lock();
    auto rpc_context = std::static_pointer_cast<RpcContext>(conn_entity->get_context());
    rpc_context->ParseInput(conn_entity->get_waitting_parse_data());
    return;
}