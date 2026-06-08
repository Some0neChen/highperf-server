#include "Timer.h"
#include <sys/timerfd.h>
#include <unistd.h>
#include <utility>
#include "Logger.h"
#include <EventLoop.h>

Timer::Timer()
{
    // 设置定时器
    itimerspec ts = { 0 };
    // 如果设置成tv_nsec，则为毫秒级，且同时需要将tv_sec设置为0，否则会被当成秒级，导致定时器间隔过长
    // 如果设置为0，则代表不启动定时器，除非后续通过timerfd_settime来设置定时器初始值，否则定时器将永远不会触发
    ts.it_interval.tv_sec = TIMER_SPEC::SERVER_INTERVAL; // 定时器间隔时间，单位秒
    ts.it_value.tv_sec = TIMER_SPEC::SERVER_INTERVAL;    // 定时器初始值，单位秒
    fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (fd_ == -1) {
        LOG_ERR("Timer create timerfd err. errorno %d.", errno);
        exit(EXIT_FAILURE);
    }
    LOG_INFO("Timer[%d] was created successfully.", fd_);
    auto ret = timerfd_settime(fd_, 0, &ts, nullptr);
    if (ret == -1) {
        LOG_ERR("Timer[%d] set timerfd time err. errorno %d.", fd_, errno);
        exit(EXIT_FAILURE);
    }
    LOG_INFO("Timer[%d] was created and inited successfully.", fd_);
}

Timer::~Timer()
{
    if (fd_ != -1) {
        close(fd_);
        fd_ = -1;
    }
}

void Timer::stop()
{
    if (!loop_) {
        return;
    }
    auto self = shared_from_this();
    loop_->queue_in_loop([self]() {
        self->shutdown();
    });
}


void Timer::shutdown()
{
    if (fd_ == -1) {
        return;
    }
    channel_.unregister_channel();
    close(fd_);
    fd_ = -1;
    channel_.set_fd(fd_);
}

// 初始化Channel及将其挂接EventLoop
void Timer::attach_loop(EventLoop* loop)
{
    loop_ = loop;
    channel_.set_fd(fd_);
    channel_.attatch_to_loop(loop_);
    channel_.enable_reading_notify();
    channel_.set_read_callback([this]() {
        return handle_timer();
    });
    channel_.register_channel();
}

// 提供给Channel的read回调
void Timer::handle_timer()
{
    // 处理定时器事件
    LOG_INFO("Timer fd[%d] Trigging.", fd_);
    // 清除定时器计数
    uint64_t trig_times;
    auto ret = read(fd_, &trig_times, sizeof(uint64_t));
    if (ret == -1) {
        LOG_ERR("Timer[%d] read timerfd err. errorno %d.", fd_, errno);
        return;
    }

    // 批量取出已经过时的时间事件，这么处理是为了可以少反复加锁，在Reactor也可以只加锁一次后批量处理
    auto now_time = std::chrono::steady_clock::now();
    if (timer_queue_.empty() || now_time < timer_queue_.top().trig_time) {
        return;
    }
    /* 当前保证定时器为Channel触发，因此同在Reactor线程
    *  因此在此直接进行Channel的定时触发操作符合预期
    *  此时也不会有其它EventLoop任务会用到Timer，因此可在锁内进行操作
    *  今后若希望复用其它定时器行为，则可以考虑把任务先全拷贝出来后放到EventLoop的queue_in_loop里
    *  但要注意先用Vector取出来后再进行传递，避免锁内加锁 */
    while (!timer_queue_.empty() && now_time > timer_queue_.top().trig_time) {
        timer_queue_.top().trigger();
        timer_queue_.pop();
    }
    return;
}

void Timer::add_timer(std::chrono::steady_clock::time_point time, std::function<void()> task)
{
    timer_queue_.emplace(std::move(time), std::move(task));
}