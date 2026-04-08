#include "../include/LogPub.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <netinet/in.h>
#include <array> // For std::array
#include <string>

// Use std::array with TIME_TYPE::COUNT for compile-time size checking and better type safety
static const std::array<const char*, static_cast<size_t>(TIME_TYPE::COUNT)> TIME_FORMAT = {
    "%Y%m%d",
    "%Y%m%d%H%M%S",
    "%Y-%m-%d %H:%M:%S"
};

std::string getTime(TIME_TYPE time_type) {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    struct tm result_tm;
    localtime_r(&now_c, &result_tm); // 相比于localtime，localtime_r是线程安全的
    char time_buffer[MAX_TIME_STRING_LENGTH];
    ssize_t date_len = strftime(time_buffer, sizeof(time_buffer), TIME_FORMAT[static_cast<size_t>(time_type)], &result_tm);

    if (date_len == 0) {
        return ""; // Return an empty string to indicate failure
    }
    
    return std::string(time_buffer, date_len);
}