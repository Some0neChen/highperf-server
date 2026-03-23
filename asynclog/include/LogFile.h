#pragma once

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "LogPub.h"

class LogFile {
    std::string dir_path;
    std::string current_date;
    std::string current_file_path;
    int file_fd;
    constexpr static unsigned int FILE_MAX_SIZE = 10 * 1024 * 1024;
    unsigned int file_size;

    void roll_if_need(const std::string& log_str) {
        if (file_size + log_str.size() < FILE_MAX_SIZE && current_date == getTime(TIME_TYPE::YMD)) {
            return;
        }
        close(file_fd);
        current_file_path = dir_path + "diag" + getTime(TIME_TYPE::YMDHMS) + ".log";
        current_date = getTime(TIME_TYPE::YMD);
        file_fd = open(current_file_path.c_str(), O_WRONLY | O_CREAT, 0644);
        if (file_fd == -1) {
            std::cerr << "Failed to open log file: " << current_file_path << strerror(errno) << std::endl;
        }
        file_size = 0;
    }
public:
    LogFile(std::string path) : dir_path(path) {
        current_date = getTime(TIME_TYPE::YMD);
        current_file_path = dir_path + "diag" + getTime(TIME_TYPE::YMDHMS) + ".log";
        file_fd = open(current_file_path.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);
        if (file_fd == -1) {
            std::cerr << "Failed to open log file: " << current_file_path << strerror(errno) << std::endl;
        }
        struct stat file_stat;
        fstat(file_fd, &file_stat);
        file_size = file_stat.st_size;
    }

    ~LogFile() {
        close(file_fd);
    }

    void write_log(const std::shared_ptr<std::vector<std::string>>& log_buffer) {
        if (file_fd == -1) {
            std::cerr << "File invalid: " << current_file_path << strerror(errno) << std::endl;
            return;
        }
        std::for_each(log_buffer->begin(), log_buffer->end(), [this](const std::string &log_str) {
            roll_if_need(log_str);
            write(file_fd, log_str.c_str(), log_str.size());
            file_size += log_str.size();
        });
    }

    LogFile(const LogFile&) = delete;
    LogFile& operator=(const LogFile&) = delete;
    LogFile(LogFile&&) = delete;
    LogFile& operator=(LogFile&&) = delete;
};