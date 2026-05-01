#include "Logger.h"
#include "LogPub.h"
#include "LogFlusher.h"
#include <string>

Logger::Logger() {
    const std::string file_dir("/home/some0nechen/文档/code/CPPServer/server/log/");
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
    char log_buffer[LOG_SPEC::MESSAGE_LEN];
    vsnprintf(log_buffer, LOG_SPEC::MESSAGE_LEN, fmt, args);
    va_end(args);

    // 只显示文件名，不显示完整路径，最终file_idx为文件名字符串的起始指针
    const char* file_idx = strrchr(file_name, '/');
    file_idx = file_idx ? file_idx + 1 : file_name;
    
    char entry_buf[LOG_SPEC::SINGLE_LOG_LEN];
    snprintf(entry_buf, LOG_SPEC::SINGLE_LOG_LEN, "[%s][%s][%-20s:%-5u][%lx]\t%s\n",
        getTime(TIME_TYPE::YMDHMS_LOG).c_str(), type == LOG_TYPE::INFO ? "INFO " : "ERROR",
        file_idx, line, thread_id, log_buffer);
    std::string entry_str(entry_buf);
    return logBuffer_->push(std::move(entry_str));
}