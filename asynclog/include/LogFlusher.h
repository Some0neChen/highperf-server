#pragma once

#include "LogFile.h"
#include "LogBuffer.h"
#include "Logger.h"
#include <memory>
#include <thread>

class Logger;
class LogFlusher {
    std::thread flusher_thread_;
    std::shared_ptr<LogFile> file_;
    std::shared_ptr<LogBuffer> buffer_;

    void flush_routine();
    void stop();
    friend class Logger;
public:
    LogFlusher(std::shared_ptr<LogFile> file, std::shared_ptr<LogBuffer> buffer);
    ~LogFlusher();

    LogFlusher(const LogFlusher&) = delete;
    LogFlusher& operator=(const LogFlusher&) = delete;
    LogFlusher(LogFlusher&&) = delete;
    LogFlusher& operator=(LogFlusher&&) = delete;
};