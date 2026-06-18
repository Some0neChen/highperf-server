#include "RpcService.h"
#include "Logger.h"
#include "RpcPub.h"
#include <cstdint>
#include <utility>

bool RpcServerManager::register_service(const uint16_t& service_id, std::unique_ptr<RpcService>&& service)
{
    auto it = service_map_.find(service_id);
    if (it != service_map_.end()) {
        return false;
    }
    service_map_.emplace(service_id, std::forward<std::unique_ptr<RpcService>>(service));
    return true;
}

uint32_t RpcServerManager::excute_service_request(std::shared_ptr<TLVData> req, std::function<void(std::shared_ptr<OutputChunk>)> done)
{
    auto it = service_map_.find(req->header.service_id);
    if (it == service_map_.end()) {
        LOG_ERR("RPC Service %u unfound.", req->header.service_id);
        return RPC_SERVICE_CODE::SERVICE_UNEXISTED;
    }
    return it->second->excute_request(req, done);
}