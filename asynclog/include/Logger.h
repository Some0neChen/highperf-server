#pragma once

#include "LogPub.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <memory>

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