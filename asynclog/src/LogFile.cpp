#include "LogFile.h"
#include "LogPub.h"
#include <bits/types/struct_iovec.h>
#include <cstddef>
#include <iostream>
#include <sys/uio.h>
#include <unistd.h>
#include <vector>


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
    if (file_fd_ != -1) {
        close(file_fd_);
        file_fd_ = -1;
        file_size_ = 0;
    }
    current_date_ = getTime(TIME_TYPE::YMD);
    current_file_path_ = dir_path_ + "diag" + getTime(TIME_TYPE::YMDHMS) + ".log";
    auto new_fd = open(current_file_path_.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (new_fd == -1) {
        std::cerr << "Failed to open log file: " << current_file_path_ << " " << strerror(errno) << std::endl;
        return;
    }
    struct stat file_stat;
    if (fstat(new_fd, &file_stat) != 0) {
        std::cerr << "Failed to stat file: " << current_file_path_ << " " << strerror(errno) << std::endl;
        close(new_fd);
        return;
    }
    file_fd_ = new_fd;
    file_size_ = file_stat.st_size;
}

void LogFile::roll_if_need(const std::shared_ptr<BufferBlock>& log_buffer) {
    bool need_roll = false;
    need_roll |= (file_size_ + log_buffer->written_bytes_ >= FILE_MAX_SIZE);
    need_roll |= (current_date_ != getTime(TIME_TYPE::YMD));
    need_roll |= ((++write_count_ %= 100) == 0 && !is_file_exist());
    if (!need_roll) {
        return;
    }
    reset_file_fd();
}

LogFile::LogFile(std::string path)
    : dir_path_(path), file_fd_(-1), file_size_(0), write_count_(0) {
    reset_file_fd();
}

LogFile::~LogFile() {
    if (file_fd_ != -1) {
        close(file_fd_);
    }
}

// 如果一次写完则返回
// 否则进入循环，调整iovecs，继续写，直到全部写完或者发生错误
RET_FLAG LogFile::writev_all(const std::shared_ptr<BufferBlock>& log_buffer) {
    auto n = writev(file_fd_, log_buffer->block_iovec_.data(), log_buffer->write_pos_);
    if (n <= 0 || n > log_buffer->written_bytes_) {
        return RET_FLAG::ERR;
    }
    auto written = static_cast<size_t>(n);
    if (written == log_buffer->written_bytes_) {
        return RET_FLAG::OK;
    }
    auto iovecs = std::vector<iovec>(log_buffer->block_iovec_.begin(), log_buffer->block_iovec_.begin() + log_buffer->write_pos_);
    size_t idx = 0;
    auto waitting_write_size = log_buffer->written_bytes_ - written;
    while (waitting_write_size > 0) {
        while (idx < iovecs.size() && written >= iovecs[idx].iov_len) {
            written -= iovecs[idx].iov_len;
            ++idx;
        }
        if (idx == iovecs.size()) {
            return RET_FLAG::ERR;
        }
        iovecs[idx].iov_base = static_cast<char*>(iovecs[idx].iov_base) + written;
        iovecs[idx].iov_len -= written;

        n = writev(file_fd_, iovecs.data() + idx, iovecs.size() - idx);
        if (n <= 0 || static_cast<size_t>(n) > waitting_write_size) {
            return RET_FLAG::ERR;
        }
        written = static_cast<size_t>(n);
        waitting_write_size -= written;
    }
    return RET_FLAG::OK;
}

void LogFile::write_log(const std::shared_ptr<BufferBlock>& log_buffer) {
    if (file_fd_ == -1) {
        std::cerr << "File invalid: " << current_file_path_ << " " << strerror(errno) << std::endl;
        return;
    }
    roll_if_need(log_buffer);
    if (file_fd_ == -1) {
        std::cerr << "File invalid after roll: " << current_file_path_ << " " << strerror(errno) << std::endl;
        return;
    }
    auto ret = writev_all(log_buffer);
    if (ret != RET_FLAG::OK) {
        std::cerr << "Failed to write log buffer to file: " << current_file_path_ << " " << strerror(errno) << std::endl;
        reset_file_fd();
        return;
    }
    file_size_ += log_buffer->written_bytes_;
}