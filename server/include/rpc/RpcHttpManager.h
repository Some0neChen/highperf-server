#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>
class RpcHttpManager {
    std::unordered_map<uint8_t, std::vector<int>> service_conns_;
}