#pragma once
#include <cstddef>
#include <string>

class Logger;

enum class RET_FLAG {
    OK = 0,
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

std::string getTime(TIME_TYPE time_type);