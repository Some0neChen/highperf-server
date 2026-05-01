#pragma once

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

class LogFile {
    std::string dir_path_;
    std::string current_date_;
    std::string current_file_path_;
    int file_fd_;
    constexpr static unsigned int FILE_MAX_SIZE = 10 * 1024 * 1024;
    unsigned int file_size_;
    unsigned int write_count_; // 记录写入的数据数量，每100次检查文件是否还存在

    bool is_file_exist();
    void reset_file_fd();
    void roll_if_need(const std::string& log_str);
public:
    LogFile(std::string path);
    ~LogFile();
    void write_log(const std::shared_ptr<std::vector<std::string>>& log_buffer);
    LogFile(const LogFile&) = delete;
    LogFile& operator=(const LogFile&) = delete;
    LogFile(LogFile&&) = delete;
    LogFile& operator=(LogFile&&) = delete;
};