#pragma once

#include "RpcPub.h"
#include <cstdint>
#include <unordered_map>
#include <memory>
#include <functional>

namespace RPC_SERVICE_CODE {
    constexpr uint32_t OK                   = 200;
    constexpr uint32_t SERVICE_UNEXISTED    = 404;
    constexpr uint32_t CONDITION_UNSATISY   = 400;
    constexpr uint32_t UNFOUND              = 404;
}

class RpcService;
class TLVData;
class OutputChunk;
class RpcServerManager {
    std::unordered_map<uint16_t, std::unique_ptr<RpcService>> service_map_;
    RpcServerManager() = default;
public:
    static RpcServerManager& get_manager() {
        static RpcServerManager manager;
        return manager;
    }

    bool register_service(const uint16_t&, std::unique_ptr<RpcService>&&);
    uint32_t excute_service_request(std::shared_ptr<TLVData>, std::function<void(std::shared_ptr<OutputChunk>)>);
};

class RpcService {
public:
    RpcService() = default;
    virtual ~RpcService() = default;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual uint32_t excute_request(std::shared_ptr<TLVData>, std::function<void(std::shared_ptr<OutputChunk>)>) = 0;
};