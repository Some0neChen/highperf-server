#pragma once

#include "OutputChunk.h"
#include "RpcService.h"
#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <sys/types.h>
#include <thread>
#include <unordered_map>
#include <vector>

namespace KVSERVICE_SPEC {
    static constexpr uint16_t SERVICE_ID = 1;
}

namespace KVSERVICE_CONST {
    constexpr std::string_view ERROR_STR = "{\"ok\":false,\"error\":";
    constexpr std::string_view SUCCESS_STR = "{\"ok\":true,";
}

enum class KVSERVICE_METHOD {
    PUT = 0,
    GET,
    SET,
    DEL,
    MAX,
};

static const std::array<std::string, static_cast<size_t>(KVSERVICE_METHOD::MAX)> KVSERVICE_METHOD_STR = {
    "PUT",
    "GET",
    "SET",
    "DEL",
};

enum class KVSERVICE_TYPE {
    KEY,
    VALUE,
    MAX,
};

static const std::array<std::string, static_cast<size_t>(KVSERVICE_TYPE::MAX)> KVSERVICE_TYPE_STR = {
    "KEY",
    "VALUE",
};

struct RequestResult {
    uint32_t retcode;
    std::string msg;
};

class KVService : public RpcService {
    bool service_enable_;
    std::thread kv_thread_;
    std::mutex kv_task_mutex_;
    std::condition_variable kv_task_cond_;
    std::queue<std::function<void()>> kv_queue_;

    void kv_running();

    std::unordered_map<std::string, std::string> kv_map_;
    std::unordered_map<KVSERVICE_METHOD, std::function<RequestResult(std::unordered_map<uint8_t, std::vector<char>>)>> method_;

    RequestResult put(std::unordered_map<uint8_t, std::vector<char>>);
    RequestResult get(std::unordered_map<uint8_t, std::vector<char>>);
    RequestResult set(std::unordered_map<uint8_t, std::vector<char>>);
    RequestResult del(std::unordered_map<uint8_t, std::vector<char>>);

    int format_error_msg(std::string&, const char*);
    int success_msg(std::string&);
    int format_get_success_msg(std::string&, const char*, const char*);

    std::shared_ptr<OutputChunk> service_exception_chunk(RpcHeader& header ,const char* errmsg);
public:
    KVService();
    ~KVService();
    void start() override;
    void stop() override;
    uint32_t excute_request(std::shared_ptr<TLVData>, std::function<void(std::shared_ptr<OutputChunk>)>) override;
};