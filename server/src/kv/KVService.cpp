#include "KVService.h"
#include "OutputChunk.h"
#include "RpcProtocol.h"
#include "RpcService.h"
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include "RpcParser.h"

void register_kv_service()
{
    // 服务端注册
    RpcServerManager::get_manager().register_service(0x1, std::make_unique<KVService>());
    // TODO 请求端注册
}

int KVService::format_error_msg(std::string& format, const char* err_msg)
{
    auto len =  snprintf(format.data(), 256, "{\"ok\":false,\"error\":\"%s\"}", err_msg);
    if (len != -1) {
        format.resize(len);
    } else {
        format.clear();
    }
    return len;
}

int KVService::success_msg(std::string& format)
{
    auto len = snprintf(format.data(), 256, "{\"ok\":true}");
    if (len != -1) {
        format.resize(len);
    } else {
        format.clear();
    }
    return len;
}

int KVService::format_get_success_msg(std::string& format, const char* key, const char* value)
{
    auto len = snprintf(format.data(), 256, "{\"ok\":true,\"key\":\"%s\",\"value\":\"%s\"}", key, value);
    if (len != -1) {
        format.resize(len);
    } else {
        format.clear();
    }
    return len;
}

RequestResult KVService::put(std::unordered_map<uint8_t, std::vector<char>> req)
{
    RequestResult ret;
    auto key_it = req.find(static_cast<uint8_t>(KVSERVICE_TYPE::KEY));
    if (key_it == req.end()) {
        this->format_error_msg(ret.msg, "Key invalid.");
        ret.retcode = RPC_SERVICE_CODE::CONDITION_UNSATISY;
        return ret;
    }
    auto val_it = req.find(static_cast<uint8_t>(KVSERVICE_TYPE::VALUE));
    if (val_it == req.end()) {
        this->format_error_msg(ret.msg, "Key-Value invalid.");
        ret.retcode = RPC_SERVICE_CODE::CONDITION_UNSATISY;
        return ret;
    }
    if (kv_map_.find(key_it->second.data()) != kv_map_.end()) {
        this->format_error_msg(ret.msg, "Key has existed.");
        ret.retcode = RPC_SERVICE_CODE::UNFOUND;
        return ret;
    }
    kv_map_.emplace(std::move(key_it->second), std::move(val_it->second));
    this->success_msg(ret.msg);
    ret.retcode = RPC_SERVICE_CODE::OK;
    return ret;
}

RequestResult KVService::get(std::unordered_map<uint8_t, std::vector<char>> req)
{
    RequestResult ret;
    auto key_it = req.find(static_cast<uint8_t>(KVSERVICE_TYPE::KEY));
    if (key_it == req.end()) {
        this->format_error_msg(ret.msg, "Key invalid.");
        ret.retcode = RPC_SERVICE_CODE::CONDITION_UNSATISY;
        return ret;
    }
    auto val_it = kv_map_.find(key_it->second.data());
    if (val_it == kv_map_.end()) {
        this->format_error_msg(ret.msg, "Value not found.");
        ret.retcode = RPC_SERVICE_CODE::UNFOUND;
        return ret;
    }
    this->format_get_success_msg(ret.msg, val_it->first.c_str(), val_it->second.c_str());
    ret.retcode = RPC_SERVICE_CODE::OK;
    return ret;
}

RequestResult KVService::set(std::unordered_map<uint8_t, std::vector<char>> req)
{
    RequestResult ret;
    auto key_it = req.find(static_cast<uint8_t>(KVSERVICE_TYPE::KEY));
    if (key_it == req.end()) {
        this->format_error_msg(ret.msg, "Key invalid.");
        ret.retcode = RPC_SERVICE_CODE::CONDITION_UNSATISY;
        return ret;
    }
    auto val_it = kv_map_.find(key_it->second.data());
    if (val_it == kv_map_.end()) {
        this->format_error_msg(ret.msg, "Value not found.");
        ret.retcode = RPC_SERVICE_CODE::UNFOUND;
        return ret;
    }
    auto kv_it = kv_map_.find(key_it->second.data());
    if (kv_it == kv_map_.end()) {
        this->format_error_msg(ret.msg, "Value not found.");
        ret.retcode = RPC_SERVICE_CODE::UNFOUND;
        return ret;
    }
    kv_it->second = std::move(val_it->second.data());
    this->success_msg(ret.msg);
    ret.retcode = RPC_SERVICE_CODE::OK;
    return ret;
}

RequestResult KVService::del(std::unordered_map<uint8_t, std::vector<char>> req)
{
    RequestResult ret;
    auto key_it = req.find(static_cast<uint8_t>(KVSERVICE_TYPE::KEY));
    if (key_it == req.end()) {
        this->format_error_msg(ret.msg, "Key invalid.");
        ret.retcode = RPC_SERVICE_CODE::CONDITION_UNSATISY;
        return ret;
    }
    auto val_it = kv_map_.find(key_it->second.data());
    if (val_it == kv_map_.end()) {
        this->format_error_msg(ret.msg, "Value not found.");
        ret.retcode = RPC_SERVICE_CODE::UNFOUND;
        return ret;
    }
    kv_map_.erase(val_it);
    this->success_msg(ret.msg);
    ret.retcode = RPC_SERVICE_CODE::OK;
    return ret;
}


KVService::KVService()
{
    method_.emplace(static_cast<uint8_t>(KVSERVICE_METHOD::PUT), &KVService::put);
    method_.emplace(static_cast<uint8_t>(KVSERVICE_METHOD::GET), &KVService::get);
    method_.emplace(static_cast<uint8_t>(KVSERVICE_METHOD::SET), &KVService::set);
    method_.emplace(static_cast<uint8_t>(KVSERVICE_METHOD::DEL), &KVService::del);
}
    
KVService::~KVService()
{
    stop();
}

void KVService::kv_running()
{
    while (true) {
        std::queue<std::function<void()>> task_queue;
        {
            std::unique_lock<std::mutex> lock(kv_task_mutex_);
            kv_task_cond_.wait(lock, [this]() {
                return !service_enable_ || !kv_queue_.empty();
            });
            if (!service_enable_ && kv_queue_.empty()) {
                break;
            }
            std::swap(kv_queue_, task_queue);
        }
        while (!task_queue.empty()) {
            task_queue.front()();
            task_queue.pop();
        }
    }
}

void KVService::start()
{
    service_enable_ = true;
    kv_thread_ = std::thread(&KVService::kv_running);
}

void KVService::stop()
{
    {
        std::lock_guard<std::mutex> lock(kv_task_mutex_);
        if (!service_enable_) {
            return;
        }
        service_enable_ = false;
    }
    kv_task_cond_.notify_all();
    if (kv_thread_.joinable()) {
        kv_thread_.join();
    }
}

std::shared_ptr<OutputChunk> KVService::service_exception_chunk(RpcHeader& header, const char* errmsg)
{
    auto chunk = std::make_shared<StringChunk>();
    header.status = RPC_SERVICE_CODE::SERVICE_UNEXISTED;
    chunk->append(static_cast<char*>(static_cast<void*>(&header)));
    std::string rsp_msg;
    format_error_msg(rsp_msg, errmsg);
    chunk->append(std::move(rsp_msg));
    return chunk;
}

uint32_t KVService::excute_request(std::shared_ptr<TLVData> data, std::function<void(std::shared_ptr<OutputChunk>)> done)
{
    auto handler = method_.find(static_cast<KVSERVICE_METHOD>(data->header.method_id));
    if (handler == method_.end()) {
        done(this->service_exception_chunk(data->header, "Method not found."));
        return RPC_SERVICE_CODE::SERVICE_UNEXISTED;
    }
    std::lock_guard<std::mutex> lock(kv_task_mutex_);
    if (!service_enable_ || !method_.count(static_cast<KVSERVICE_METHOD>(data->header.method_id))) {
        done(this->service_exception_chunk(data->header, "Service closed."));
        return RPC_SERVICE_CODE::SERVICE_UNEXISTED;
    }
    kv_queue_.emplace([this, data, done, handler] () {
        auto chunk = std::make_shared<StringChunk>();
        auto ret = handler->second(data->payload);
        data->header.status = ret.retcode;
        hton_rpc_header(data->header);
        chunk->append(static_cast<char*>(static_cast<void*>(&data->header)));
        chunk->append(std::move(ret.msg));
        done(chunk);
    });
    return RPC_SERVICE_CODE::OK;
}