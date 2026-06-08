#include "EventTrigger.h"
#include "EventLoop.h"
#include "Logger.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unistd.h>
#include <utility>
#include <sys/eventfd.h>

EventTrigger::EventTrigger(EventLoop* loop) : channel_(-1, loop)
{
    task_queue_.reserve(EVENTTRIGGER_SPEC::TASK_QUEUE_INIT_SIZE);
    event_fd_ = eventfd(0, EFD_NONBLOCK);
    if (event_fd_ == -1) {
        LOG_ERR("EventLoop create wakefd err. errorno %d.", errno);
        exit(EXIT_FAILURE);
    }
    channel_.set_fd(event_fd_);
    channel_.enable_reading_notify();
    channel_.set_close_callback([]() {
        exit(EXIT_FAILURE);
    });
    channel_.set_read_callback([this](){
        return do_pending_tasks();
    });
}

EventTrigger::~EventTrigger()
{
    close_trigger();
}

void EventTrigger::close_trigger()
{
    if (channel_.get_event() != 0x0) {
        channel_.unregister_channel();
    }
    if (event_fd_ != -1) {
        close(event_fd_);
    }
    event_fd_ = -1;
    return;
}

void EventTrigger::add_task(std::function<void()> task)
{
    {
        std::lock_guard<std::mutex> lock(task_queue_mutex_);
        task_queue_.push_back(std::move(task));
    }
}

void EventTrigger::wake_up()
{
    if (event_fd_ == -1) {
        LOG_ERR("EventLoop eventfd[%d] pending loop already closed. eventfd is invalid.", event_fd_);
        return;
    }
    /* 通过向wake_fd写入数据来唤醒epoll监听线程，此处应该读写8字节 */
    uint64_t val = 1;
    /* 唤醒epoll监听线程，通知有事务 */
    auto ret = write(event_fd_, &val, sizeof(val)); 
    if (ret < 0) {
        LOG_ERR("EventLoop eventfd[%d] wake up err. errorno %d. reason %s.", event_fd_, errno, strerror(errno));
    }
}

void EventTrigger::do_pending_tasks()
{
    handle_wake_up();
    auto task_queue = std::vector<std::function<void()>>();
    task_queue.reserve(EVENTTRIGGER_SPEC::TASK_QUEUE_INIT_SIZE);
    {
        std::lock_guard<std::mutex> lock(task_queue_mutex_);
        task_queue.swap(task_queue_);
    }
    for (const auto& task : task_queue) {
        task();
    }
}

void EventTrigger::handle_wake_up()
{
    if (event_fd_ == -1) {
        LOG_ERR("EventLoop eventfd[%d] pending loop already closed. wake_fd is invalid.", event_fd_);
        return;
    }
    uint64_t val;
    while(true) {
        auto ret = read(event_fd_, &val, sizeof(val)); // 读取wake_fd，关闭事件，防止重复触发
        if (ret == sizeof(val)) {
            // 读取成功
            break;
        }
        if (ret == -1 && errno == EINTR) {
            // 系统信号中断，继续读取
            continue;
        }
        if (ret == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // 已经读完，退出循环
            break;
        }
        LOG_ERR("EventLoop eventfd[%d] handle wake up err. errorno %d.", event_fd_, errno);
        return;
    }
}

void EventTrigger::register_to_loop()
{
    channel_.register_channel();
}