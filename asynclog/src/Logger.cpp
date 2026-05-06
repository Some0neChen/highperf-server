#include "Logger.h"
#include "LogPub.h"
#include "LogFlusher.h"
#include <cstddef>
#include <cstring>
#include <string>

Logger::Logger() {
    const std::string file_dir("/home/some0nechen/文档/code/CPPServer/diaglog/");
    logFile_ = std::make_shared<LogFile>(file_dir);
    logBuffer_ = std::make_shared<LogBuffer>();
    logFlusher_ = std::make_shared<LogFlusher>(logFile_, logBuffer_);
}

Logger& Logger::getInstance() {
    static Logger G_LOGGER;
    return G_LOGGER;
}

Logger::~Logger() {
    logFlusher_->stop();
}

RET_FLAG Logger::log(const LOG_TYPE type, const char* file_name, const unsigned int line, const pthread_t& thread_id,
    const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char log_buffer[LOG_SPEC::SINGLE_LOG_LEN];
    vsnprintf(log_buffer, LOG_SPEC::SINGLE_LOG_LEN, fmt, args);
    va_end(args);

    // 只显示文件名，不显示完整路径，最终file_idx为文件名字符串的起始指针
    const char* file_idx = strrchr(file_name, '/');
    file_idx = file_idx ? file_idx + 1 : file_name;
    
    char entry_buf[LOG_SPEC::SINGLE_LOG_LEN];
    auto date = getCachedLogTime();
    auto ret = snprintf(entry_buf, LOG_SPEC::SINGLE_LOG_LEN, "[%.*s][%s][%-20s:%-5u][%lx]\t%s\n",
        static_cast<int>(date.size()), date.data(), type == LOG_TYPE::INFO ? "INFO " : "ERROR",
        file_idx, line, thread_id, log_buffer);
    size_t len = 0;
    if (ret < 0) {
        return RET_FLAG::ERR;
    }
    if (static_cast<size_t>(ret) >= LOG_SPEC::SINGLE_LOG_LEN) {
        // 截断日志，不覆盖最后的'\0'
        memcpy(entry_buf + LOG_SPEC::SINGLE_LOG_LEN - LOG_SPEC::LOG_TRUNCATE_SUFFIX.length() - 1,
            LOG_SPEC::LOG_TRUNCATE_SUFFIX.data(), LOG_SPEC::LOG_TRUNCATE_SUFFIX.length());
        // 不写入'\0'
        len = LOG_SPEC::SINGLE_LOG_LEN - 1;
    } else {
        len = static_cast<size_t>(ret);
    }
    return logBuffer_->push(entry_buf, len);
}