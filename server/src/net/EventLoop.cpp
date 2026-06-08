#include "EventLoop.h"
#include "Logger.h"
#include <functional>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include "Channel.h"

EventLoop::EventLoop() : event_trigger_(this)
{
    // 预分配events_数量，减少频繁扩容
    events_.reserve(EVENTLOOP_SPEC::EVENT_INIT_NUM);
    events_.resize(EVENTLOOP_SPEC::EVENT_INIT_NUM);

    // epoll 创建及监听wake_fd，reactor触发析构时通过wake_fd来结束监听线程
    epoll_fd_ = epoll_create(1);
    if (epoll_fd_ == -1) {
        LOG_ERR("Epoll create err.");
        exit(EXIT_FAILURE);
    }
    event_trigger_.register_to_loop();
    LOG_INFO("Reactor[%d] initialization successful.", epoll_fd_);
}

void EventLoop::start()
{
    quit_.store(false);
    loop_thread_ = std::thread(&EventLoop::loop, this);
    return;
}

EventLoop::~EventLoop()
{
    quit_.store(true);
    event_trigger_.wake_up();
    if (loop_thread_.joinable()) {
        loop_thread_.join();
    }
    event_trigger_.close_trigger();
    close(epoll_fd_);
}

bool EventLoop::is_in_loop_thread()
{
    return std::this_thread::get_id() == loop_thread_.get_id();
}

void EventLoop::loop()
{
    LOG_INFO("EventLoop Reactor[%d] running.", this->epoll_fd_);
    while (!quit_.load()) {
        auto event_nums = epoll_wait(this->epoll_fd_, events_.data(),
            EVENTLOOP_SPEC::EVENT_INIT_NUM, -1);
        if (event_nums == -1) {
            if (errno == EINTR) {
                LOG_INFO("System interrupt EventLoop Reactor[%d].", epoll_fd_);
                continue;
            }
            // TODO 是否为异常需要直接退出
            LOG_ERR("EventLoop Reactor[%d] occur error! errno %u errno reason %s.", epoll_fd_, errno, strerror(errno));
            continue;
        }
        for (int i = 0; i < event_nums; ++i) {
            auto channel = static_cast<Channel*>(events_[i].data.ptr);
            channel->set_received_event(events_[i].events);
            channel->handle_event();
        }
    }
}

void EventLoop::queue_in_loop(std::function<void()> task)
{
    event_trigger_.add_task(std::move(task));
    event_trigger_.wake_up();
    return;
}

void EventLoop::run_in_loop_thread(std::function<void()> task)
{
    if (is_in_loop_thread()) {
        task();
        return;
    }
    queue_in_loop(std::move(task));
}

void EventLoop::register_channel(Channel* channel)
{
    epoll_event event;
    event.events = channel->get_event();
    event.data.ptr = static_cast<void*>(channel);
    auto ret = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, channel->get_fd(), &event);
    if (ret == -1) {
        LOG_ERR("EventLoop Reactor[%d] register_channel[%d] err. errono %d err reason %s",
            epoll_fd_, channel->get_fd(), errno, strerror(errno));
        
        channel->close_channel();
        return;
    }
    LOG_INFO("EventLoop Reactor[%d] register_channel[%d] listen event[%u] successfully.",
        epoll_fd_, channel->get_fd(), channel->get_event());
    return;
}

void EventLoop::update_channel(Channel* channel)
{
    epoll_event event;
    event.events = channel->get_event();
    event.data.ptr = static_cast<void*>(channel);
    auto ret = epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, channel->get_fd(), &event);
    if (ret == -1) {
        LOG_ERR("EventLoop Reactor[%d] update_channel[%d] failed. errono %d err reason %s",
            epoll_fd_, channel->get_fd(), errno, strerror(errno));
        channel->close_channel();
        return;
    }
    LOG_INFO("EventLoop Reactor[%d] update_channel[%d] listen event[%u] successfully.",
        epoll_fd_, channel->get_fd(), channel->get_event());
    return;
}

void EventLoop::unregister_channel(Channel* channel)
{
    auto ret = epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, channel->get_fd(), nullptr);
    if (ret == -1) {
        LOG_ERR("EventLoop Reactor[%d] unregister_channel err. errono %d err reason %s",
            epoll_fd_, errno, strerror(errno));
        return;
    }
    LOG_INFO("EventLoop Reactor[%d] unregister_channel[%d] successfully.", epoll_fd_, channel->get_fd());
}