#include "HttpServer.h"
#include "HttpContext.h"
#include <memory>

void HttpServer::set_http_context(std::shared_ptr<TCPConnection> tcp_conn)
{
    auto http_context = std::make_shared<HttpContext>();
    http_context->set_dispatch_request_handle([this](std::function<void()> handle) {
        this->tpool_.add_task(handle);
    });
    tcp_conn->set_context(http_context);
    http_context->set_conn(tcp_conn);
    return;
}

void HttpServer::read_tcp_buffer(std::weak_ptr<TCPConnection> conn)
{
    auto conn_entity = conn.lock();
    auto http_context = std::static_pointer_cast<HttpContext>(conn_entity->get_context());
    http_context->ParseInput(conn_entity->get_waitting_parse_data());
    return;
}