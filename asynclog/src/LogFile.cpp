#include "LogFile.h"
#include "LogPub.h"
#include <algorithm>
#include <iostream>


bool LogFile::is_file_exist()
{
    struct stat fd_stat;
    struct stat path_stat;
    if (fstat(file_fd_, &fd_stat) != 0) {
        std::cerr << "Failed to stat file fd descriptor: " << file_fd_ << strerror(errno) << std::endl;
        return false;
    }
    if (stat(current_file_path_.c_str(), &path_stat) != 0) {
        std::cerr << "Failed to stat file path: " << current_file_path_ << strerror(errno) << std::endl;
        return false;
    }
    return fd_stat.st_ino == path_stat.st_ino;
}

void LogFile::reset_file_fd() {
    current_date_ = getTime(TIME_TYPE::YMD);
    current_file_path_ = dir_path_ + "diag" + getTime(TIME_TYPE::YMDHMS) + ".log";
    file_fd_ = open(current_file_path_.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (file_fd_ == -1) {
        std::cerr << "Failed to open log file: " << current_file_path_ << strerror(errno) << std::endl;
    }
    struct stat file_stat;
    fstat(file_fd_, &file_stat);
    file_size_ = file_stat.st_size;
}

void LogFile::roll_if_need(const std::string& log_str) {
    bool need_roll = false;
    need_roll |= (file_size_ + log_str.size() >= FILE_MAX_SIZE);
    need_roll |= (current_date_ != getTime(TIME_TYPE::YMD));
    need_roll |= ((++write_count_ %= 100) == 0 && !is_file_exist());
    if (!need_roll) {
        return;
    }
    close(file_fd_);
    reset_file_fd();
}

LogFile::LogFile(std::string path) : dir_path_(path) {
    file_size_ = 0;
    write_count_ = 0;
    reset_file_fd();
}

LogFile::~LogFile() {
    close(file_fd_);
}

void LogFile::write_log(const std::shared_ptr<std::vector<std::string>>& log_buffer) {
    if (file_fd_ == -1) {
        std::cerr << "File invalid: " << current_file_path_ << strerror(errno) << std::endl;
        return;
    }
    ssize_t total_written = 0;
    std::for_each(log_buffer->begin(), log_buffer->end(), [this, &total_written](const std::string &log_str) {
        roll_if_need(log_str);
        total_written = write(file_fd_, log_str.c_str(), log_str.size());
        if (total_written == -1) {
            std::cerr << "Failed to write log: " << current_file_path_
                << " entry dropped: "<< strerror(errno) << std::endl;
            this->reset_file_fd();
            return;
        }
        file_size_ += log_str.size();
    });
}
