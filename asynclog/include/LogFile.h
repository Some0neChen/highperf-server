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
#include <iostream>
#include "LogPub.h"

class LogFile {
    std::string dir_path;
    std::string current_date;
    std::string current_file_path;
    int file_fd;
    constexpr static unsigned int FILE_MAX_SIZE = 10 * 1024 * 1024;
    unsigned int file_size;
    unsigned int write_count_; // 记录写入的数据数量，每100次检查文件是否还存在

    bool is_file_exist()
    {
        struct stat fd_stat;
        struct stat path_stat;
        if (fstat(this->file_fd, &fd_stat) != 0) {
            std::cerr << "Failed to stat file fd descriptor: " << this->file_fd << strerror(errno) << std::endl;
            return false;
        }
        if (stat(this->current_file_path.c_str(), &path_stat) != 0) {
            std::cerr << "Failed to stat file path: " << this->current_file_path << strerror(errno) << std::endl;
            return false;
        }
        return fd_stat.st_ino == path_stat.st_ino;
    }

    void reset_file_fd() {
        this->current_date = getTime(TIME_TYPE::YMD);
        this->current_file_path = dir_path + "diag" + getTime(TIME_TYPE::YMDHMS) + ".log";
        this->file_fd = open(current_file_path.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);
        if (file_fd == -1) {
            std::cerr << "Failed to open log file: " << current_file_path << strerror(errno) << std::endl;
        }
        struct stat file_stat;
        fstat(this->file_fd, &file_stat);
        file_size = file_stat.st_size;
    }

    void roll_if_need(const std::string& log_str) {
        bool need_roll = false;
        need_roll |= (file_size + log_str.size() >= FILE_MAX_SIZE);
        need_roll |= (current_date != getTime(TIME_TYPE::YMD));
        need_roll |= ((++this->write_count_ %= 100) == 0 && !is_file_exist());
        if (!need_roll) {
            return;
        }
        close(file_fd);
        reset_file_fd();
    }
public:
    LogFile(std::string path) : dir_path(path) {
        reset_file_fd();
    }

    ~LogFile() {
        close(file_fd);
    }

    void write_log(const std::shared_ptr<std::vector<std::string>>& log_buffer) {
        if (file_fd == -1) {
            std::cerr << "File invalid: " << current_file_path << strerror(errno) << std::endl;
            return;
        }
        ssize_t total_written = 0;
        std::for_each(log_buffer->begin(), log_buffer->end(), [this, &total_written](const std::string &log_str) {
            roll_if_need(log_str);
            total_written = write(file_fd, log_str.c_str(), log_str.size());
            if (total_written == -1) {
                std::cerr << "Failed to write log: " << current_file_path
                    << " entry dropped: "<< strerror(errno) << std::endl;
                this->reset_file_fd();
                return;
            }
            file_size += log_str.size();
        });
    }

    LogFile(const LogFile&) = delete;
    LogFile& operator=(const LogFile&) = delete;
    LogFile(LogFile&&) = delete;
    LogFile& operator=(LogFile&&) = delete;
};