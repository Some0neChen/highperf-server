#include "LogFlusher.h"
#include <pthread.h>

void LogFlusher::flush_routine() {
    std::shared_ptr<std::vector<std::string>> buffer_data;
    while (true) {
        auto state = buffer_->wait_for_flush_or_timeout(3);
        while ((buffer_data = buffer_->get_flushing_buffer())) {
            file_->write_log(buffer_data);
            buffer_->recycle_buffer(buffer_data);
            buffer_data = nullptr;
        }
        if (state == BUFFER_TRIGGER_STATE::CLOSE) {
            break;
        }
    }
}

void LogFlusher::stop() {
    buffer_->stop();
    if (flusher_thread_.joinable()) {
        flusher_thread_.join();
    }
}

LogFlusher::LogFlusher(std::shared_ptr<LogFile> file, std::shared_ptr<LogBuffer> buffer) : file_(file), buffer_(buffer) {
    flusher_thread_ = std::thread([this]() {
        this->flush_routine();
    });
}

LogFlusher::~LogFlusher() {
    stop();
}