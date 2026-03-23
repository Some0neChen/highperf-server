#pragma once

#include "LogFile.h"
#include "LogBuffer.h"
#include "LogPub.h"
#include "LogFlusher.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <string>

class Logger {
    std::shared_ptr<LogFile> logFile;
    std::shared_ptr<LogFlusher> logFlusher;
    std::shared_ptr<LogBuffer> logBuffer;

    Logger() {
        const std::string file_dir("/home/some0nechen/文档/code/CPPServer/asynclog/log/");
        logFile = std::make_shared<LogFile>(file_dir);
        logBuffer = std::make_shared<LogBuffer>();
        logFlusher = std::make_shared<LogFlusher>(logFile, logBuffer);
    }

public:
    static Logger& getInstance() {
        static Logger G_LOGGER;
        return G_LOGGER;
    }

    ~Logger() {
        logBuffer->force_swap_buffer();
        logFlusher->force_flush();
    }

    RET_FLAG log(const LOG_TYPE type, const char* file_name, const unsigned int line,
        const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        char log_buffer[2048];
        vsnprintf(log_buffer, 2048, fmt, args);
        va_end(args);

        const char* file_idx = strrchr(file_name, '/');
        file_idx = file_idx ? file_idx + 1 : file_name;
        
        char entry_buf[4096];
        snprintf(entry_buf, 4096, "[%s][%s][%s:%u]%s\n",
            getTime(TIME_TYPE::YMDHMS_LOG).c_str(), type == LOG_TYPE::INFO ? "INFO " : "ERROR",
            file_idx, line, log_buffer);
        std::string entry_str(entry_buf);
        return logBuffer->push(entry_str);
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;
};