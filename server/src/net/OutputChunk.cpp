#include "OutputChunk.h"
#include "Logger.h"
#include <sys/mman.h>
#include <sys/socket.h>
#include <utility>

StringChunk::StringChunk()
{ 
    response_data_.reserve(OUTPUT_CHUNK_SPEC::HTTP_RESPOND_MSG_SIZE);
}

StringChunk& StringChunk::append(std::string&& data)
{
    pending_write_bytes_ += data.size();
    response_data_.append(std::forward<std::string>(data));
    return *this;
}

StringChunk& StringChunk::append(const std::string_view& data)
{
    response_data_.append(data);
    pending_write_bytes_ += data.size();
    return *this;
}

WriteOutCome StringChunk::writeToSocket(const int& fd)
{
    ssize_t write_len = 0;
    while (true) {
        write_len = send(fd, response_data_.data() + written_bytes_,
            pending_write_bytes_ - written_bytes_, MSG_NOSIGNAL);

        if (write_len == -1) {
            if (errno == EINTR) {
                LOG_INFO("System interrupt listen TCP Connection[%d] to write [str]tcp response packet. errono[%d]", fd, errno);
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                LOG_INFO("TCP Connection[%d] trig sending block[str]. errono[%d]", fd, errno);
                break;
            }
            LOG_ERR("TCP Connection[%d] send back msg failed[str]. ret %d errno %d errmsg %s",
                fd, write_len, errno, strerror(errno));
            return {TCPWriteResult::ERROR, 0};
        }

        if (write_len == 0) {
            LOG_INFO("TCP Connection[%d] trig sending block[str] with send return 0.", fd);
            return {TCPWriteResult::PARTIAL, written_bytes_};
        }

        written_bytes_ += write_len;
        LOG_INFO("[Str]TCP Connection[%d] send back len [%d] msg success. Already sended response bytes [%u], "
            "Total [%u].", fd, write_len, written_bytes_, pending_write_bytes_);
        if (written_bytes_ == pending_write_bytes_) {
            return {TCPWriteResult::COMPLETE, written_bytes_};
        }
    }
    return {TCPWriteResult::PARTIAL, written_bytes_};
}

void MMapFileChunk::set_chunk(const size_t& file_size, void* file_data)
{
    file_size_ = file_size;
    mmap_addr_ = file_data;
    iov_[0].iov_len = header_size_;
    iov_[0].iov_base = header_data_.data();
    iov_[1].iov_len = file_size_;
    iov_[1].iov_base = mmap_addr_;
    pending_write_bytes_ = header_size_ + file_size_;
}

MMapFileChunk& MMapFileChunk::append(const std::string_view& data)
{
    header_data_.append(data);
    header_size_ += data.size();
    return *this;
}

MMapFileChunk& MMapFileChunk::append(std::string&& data)
{
    header_data_.append(data);
    header_size_ += data.size();
    return *this;
}

WriteOutCome MMapFileChunk::writeToSocket(const int& fd)
{
    ssize_t write_len = 0;
    while (true) {
        write_len = writev(fd, iov_.data(), iov_.size());

        if (write_len == -1) {
            if (errno == EINTR) {
                LOG_INFO("System interrupt listen TCP Connection[%d] to write [mmap]tcp response packet. errono[%d]", fd, errno);
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                LOG_INFO("TCP Connection[%d] trig sending block[mmap]. errono[%d]", fd, errno);
                break;
            }
            LOG_ERR("TCP Connection[%d] send back msg failed[mmap]. ret %d errno %d errmsg %s",
                fd, write_len, errno, strerror(errno));
            return {TCPWriteResult::ERROR, 0};
        }

        if (write_len == 0) {
            LOG_INFO("TCP Connection[%d] trig sending block[mmap] with writev return 0.", fd);
            return {TCPWriteResult::PARTIAL, written_bytes_};
        }

        written_bytes_ += write_len;
        LOG_INFO("MMAP TCP Connection[%d] send back len [%d] msg success. Already sended response bytes [%u], "
            "Total [%u].", fd, write_len, written_bytes_, pending_write_bytes_);
        if (pending_write_bytes_ == written_bytes_) {
            return {TCPWriteResult::COMPLETE, written_bytes_};
        }

        if (write_len >= iov_[0].iov_len) {
            write_len -= iov_[0].iov_len;
            iov_[0].iov_len = 0;
        } else {
            iov_[0].iov_len -= write_len;
            iov_[0].iov_base = static_cast<char*>(iov_[0].iov_base) + write_len;
            continue;
        }

        iov_[1].iov_base = (static_cast<char*>(iov_[1].iov_base) + write_len);
        iov_[1].iov_len -= write_len;
    }
    return {TCPWriteResult::PARTIAL, written_bytes_};
}

MMapFileChunk::~MMapFileChunk()
{
    if (this->mmap_addr_ && this->file_size_ > 0) {
        munmap(this->mmap_addr_, this->file_size_);
    }
}