#include "../include/LogPub.h"

#include <arpa/inet.h>
#include <bits/types/timer_t.h>
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
    size_t date_len = strftime(time_buffer, sizeof(time_buffer), TIME_FORMAT[static_cast<size_t>(time_type)], &result_tm);

    if (date_len == 0) {
        return ""; // Return an empty string to indicate failure
    }
    
    return std::string(time_buffer, date_len);
}

std::string_view getCachedLogTime() {
    thread_local time_t cache_sec = 0;
    thread_local char cached_time[MAX_TIME_STRING_LENGTH] = {};
    thread_local unsigned short date_len = 0;

    time_t now = time(nullptr);
    if (now != cache_sec) {
        cache_sec = now;
        struct tm tm_info;
        localtime_r(&cache_sec, &tm_info);
        date_len = strftime(cached_time, sizeof(cached_time),
            TIME_FORMAT[static_cast<size_t>(TIME_TYPE::YMDHMS_LOG)], &tm_info);
    }
    return std::string_view(cached_time, date_len);
}