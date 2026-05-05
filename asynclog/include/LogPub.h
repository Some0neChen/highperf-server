#pragma once
#include <cstddef>
#include <string>

enum class RET_FLAG {
    OK = 0,
    UNENABLE,
    ERR
};

enum class LOG_TYPE {
    INFO = 0,
    ERR = 1,
    COUNT
};

constexpr static size_t MAX_TIME_STRING_LENGTH = 20; // Max length for "%Y-%m-%d %H:%M:%S" (19 chars) + null terminator
enum class TIME_TYPE {
    YMD = 0,    // yyyymmdd
    YMDHMS,     // yyyymmddhhmmss
    YMDHMS_LOG, // yyyy-mm-dd hh-mm-ss
    COUNT       // Number of time types
};

namespace LOG_SPEC {
    constexpr size_t BUFFER_POOL_INIT_SIZE = 4; // 缓冲池初始大小
    constexpr size_t SINGLE_LOG_LEN  = 4096;    // 完整日支行最大长度
    constexpr size_t MESSAGE_LEN = 2048;        // 用户消息体最大长度
}

enum class BUFFER_TRIGGER_STATE {
    FLUSH = 0,
    CLOSE,
    TIME_OUT
};

std::string getTime(TIME_TYPE time_type);