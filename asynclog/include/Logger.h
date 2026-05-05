#pragma once

#include "LogPub.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <memory>

#define LOG_INFO(fmt, ...) \
    Logger::getInstance().log(LOG_TYPE::INFO, __FILE__, __LINE__, pthread_self(), fmt, ##__VA_ARGS__)
#define LOG_ERR(fmt, ...) \
    Logger::getInstance().log(LOG_TYPE::ERR, __FILE__, __LINE__, pthread_self(), fmt, ##__VA_ARGS__)

class LogFile;
class LogFlusher;
class LogBuffer;
class Logger {
    std::shared_ptr<LogFile> logFile_;
    std::shared_ptr<LogFlusher> logFlusher_;
    std::shared_ptr<LogBuffer> logBuffer_;
    Logger();
public:
    static Logger& getInstance();
    ~Logger();
    RET_FLAG log(const LOG_TYPE type, const char* file_name, const unsigned int line, const pthread_t& thread_id,
        const char* fmt, ...);
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;
};