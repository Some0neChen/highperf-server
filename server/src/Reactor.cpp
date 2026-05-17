#include <Reactor.h>
#include "EventHandler.h"
#include "Logger.h"
#include "ServerPub.h"
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

void Reactor::reactor_running_thread()
{
    epoll_event events[MAX_EVENTS];
    std::function<EVENT_STATUS(unsigned int)> task;
    LOG_INFO("Reactor[%d] running.", this->epoll_fd_);
    while (reactor_running_state_.load()) {
        auto trig_times = epoll_wait(this->epoll_fd_, events, MAX_EVENTS, -1);
        if (trig_times == -1) {
            if (errno == EINTR) {
                LOG_INFO("System interrupt Epoll[%d].", epoll_fd_);
                continue;
            }
            LOG_ERR("Epoll[%d] occur error! errno %u errno reason %s.", epoll_fd_, errno, strerror(errno));
            continue;
        }
        for (int i = 0; i < trig_times; ++i) {
            if (events[i].data.fd == this->wake_fd_) {
                handle_wake_up();
                do_pending_tasks();
                continue;
            }
            auto find_iter = connections_.find(events[i].data.fd);
            if (find_iter == connections_.end() || find_iter->second == nullptr) { // at会抛出异常，此处用find
                LOG_ERR("Reactor[%d] find handler[%d] err.", this->epoll_fd_, events[i].data.fd);
                continue;
            }
            auto handler = find_iter->second;
            auto state = events[i].events;
            auto ret = handler->handle_event(state);
        }
    }
}

// 判断fd所挂接的ClientHandler是否还在，防止读空内存
bool Reactor::is_client_exist(const int& fd)
{
    return this->connections_.find(fd) != this->connections_.end() && this->connections_[fd] != nullptr;
}


Reactor::Reactor(std::shared_ptr<ThreadPool<std::function<EVENT_STATUS()>>>& thread_pool) :
    thread_pool_(thread_pool) {
    // 规定该reactor最多可接待的连接数，超过后将自动扩容
    conn_size_ = 1024;

    // epoll 创建及监听wake_fd，reactor触发析构时通过wake_fd来结束监听线程
    epoll_fd_ = epoll_create(1);
    if (epoll_fd_ == -1) {
        LOG_ERR("epoll create err.");
        exit(EXIT_FAILURE);
    }
    this->wake_fd_ = eventfd(0, EFD_NONBLOCK);
    if (wake_fd_ == -1) {
        LOG_ERR("Reactor[%d] create wakefd err. errorno %d.", this->epoll_fd_, errno);
        exit(EXIT_FAILURE);
    }
    epoll_event event;
    event.data.fd = this->wake_fd_;
    event.events = EPOLLIN | EPOLLET;
    auto ret = epoll_ctl(this->epoll_fd_, EPOLL_CTL_ADD, this->wake_fd_, &event);
    if (ret == -1) {
        LOG_ERR("Reactor[%d] initialized fail. errono %d err reason %s.",
            epoll_fd_, errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
    LOG_INFO("Reactor[%d] initialization. Connection pool size %u.", epoll_fd_, conn_size_);

    // 挂接定时器，用于定时检测是否有长时间不活跃的客户端并定时清除
    // TimerHandler的构造函数会进行timerfd的创建和设置
    this->timer_handler_ = std::make_shared<TimerHandler>(this);
    auto add_ret = this->add_connection(this->timer_handler_->get_timer_fd(),
        EPOLLIN | EPOLLET, this->timer_handler_);
    if (add_ret != EVENT_STATUS::OK) {
        LOG_INFO("[%s]Reactor[%d] init timer failed. errno %u err reason %s",
            __func__, this->epoll_fd_, errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
    LOG_INFO("[%s]Reactor[%d] add timer connection return %d.", __func__, this->epoll_fd_, add_ret);

    connections_.reserve(conn_size_);
    reactor_running_state_.store(true);
    reactor_thread_ = std::thread(&Reactor::reactor_running_thread, this);
}

void Reactor::reset_connection(const int& fd)
{
    if (connections_.find(fd) == connections_.end()) {
        return;
    }
    auto ret = epoll_ctl(this->epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    if (ret == -1) {
        LOG_ERR("epoll del fd[%d] err.", this->epoll_fd_);
    }
    connections_[fd] = nullptr;
    this->connections_.erase(fd); 
}

// 批量重置连接
// 主要用于定时器批量检测到过期连接时进行批量重置，减少加锁的次数
// param：expired_conns 过期连接列表，包含连接fd和对应的version_no，如果version_no说明是已被刷新的事件，不进行处理
void Reactor::timer_reset_batch_conns(const std::shared_ptr<std::vector<std::pair<int, unsigned int>>>& expired_conns)
{
    std::vector<int> actual_expired_conns;
    for (auto conn : *expired_conns) {
        // 有可能连接已经断开了，但是对应的fd事件还保存在timer的堆里，这里要做预防，防止踩空
        if (!this->is_client_exist(conn.first)) {
            continue;
        }
        if (dynamic_cast<ClientHandler*>(this->connections_[conn.first].get())->get_version_no() != conn.second) {
            continue;
        }
        LOG_INFO("[%s] Client fd[%d] have expired.", __func__, conn.first);
        actual_expired_conns.push_back(conn.first);
    }
    for (auto conn : actual_expired_conns) {
        this->reset_connection(conn);
    }
}

Reactor::~Reactor() { // 担心不加锁有问题
    stop_reactor();
    if (reactor_thread_.joinable()) {
        reactor_thread_.join();
    }
    close(this->epoll_fd_);
    close(this->wake_fd_);
}

EVENT_STATUS Reactor::add_connection(const int& fd, unsigned int listen_state,
    std::shared_ptr<EventHandler> conn)
{
    // 隐患：在加锁内进行数据结构的find操作，时间复杂度最坏可能达到O(n)，这里是一个将来高延迟的可疑点
    // std::lock_guard<std::mutex> lock(conn_mutex_);
    if (connections_.find(fd) != connections_.end() && connections_[fd] != nullptr) {
        LOG_ERR("Reactor[%d] add socket_fd[%d] err. The fd connection is already exist.", this->epoll_fd_, fd);
        return EVENT_STATUS::CLIENT_USING_ERR;
    }
    if (connections_.size() >= conn_size_) {
        conn_size_ += (conn_size_>>1);
        connections_.reserve(conn_size_);
        LOG_INFO("Reactor[%d] enlarge successfully, new size %u.", this->epoll_fd_, conn_size_);
    }

    epoll_event event;
    event.events = listen_state;
    event.data.fd = fd;
    auto ret = epoll_ctl(this->epoll_fd_, EPOLL_CTL_ADD, fd, &event);
    if (ret == -1) {
        LOG_ERR("Reactor[%d] add socket_fd err.", this->epoll_fd_);
        return EVENT_STATUS::EPOLL_ADD_ERR;
    }
    connections_[fd] = conn;
    LOG_INFO("Reactor[%d] add socket_fd[%d] successfully.", this->epoll_fd_, fd);
    return EVENT_STATUS::OK;
}

int Reactor::get_epoll_fd() const
{
    return this->epoll_fd_;
}

void Reactor::add_timer_task(const int &fd, const unsigned int &version_no,
    const std::chrono::steady_clock::time_point &time_point)
{
    this->timer_handler_->add_timer(fd, version_no, time_point);
}

EVENT_STATUS Reactor::stop_reactor()
{
    run_in_loop([this]() {
        auto ret =  remove_all_conns();
        reactor_running_state_.store(false);
        return ret;
    });
    return this->wake_up();
}

EVENT_STATUS Reactor::queue_in_loop(reactor_task task)
{
    {
        std::lock_guard<std::mutex> lock(task_mutex_);
        this->pending_tasks_.push(std::move(task));
    }
    return this->wake_up();
}

EVENT_STATUS Reactor::run_in_loop(reactor_task task)
{
    if (is_in_loop_thread()) {
        task();
        return EVENT_STATUS::OK;
    }
    queue_in_loop(task);
    return EVENT_STATUS::OK;
}

EVENT_STATUS Reactor::wake_up()
{
    if (wake_fd_ == -1) {
        LOG_ERR("Reactor[%d] pending loop already closed. wake_fd is invalid.", this->epoll_fd_);
        return EVENT_STATUS::REACTOR_WAKEUP_ERR;
    }
    /* 通过向wake_fd写入数据来唤醒epoll监听线程，此处应该读写8字节 */
    uint64_t val = 1;
    /* 唤醒epoll监听线程，通知有事务 */
    auto ret = write(this->wake_fd_, &val, sizeof(val)); 
    if (ret < 0) {
        LOG_ERR("Reactor[%d] wake up err. errorno %d.", this->epoll_fd_, errno);
        return EVENT_STATUS::REACTOR_WAKEUP_ERR;
    }
    return EVENT_STATUS::OK;
}

EVENT_STATUS Reactor::handle_wake_up()
{
    if (wake_fd_ == -1) {
        LOG_ERR("Reactor[%d] pending loop already closed. wake_fd is invalid.", this->epoll_fd_);
        return EVENT_STATUS::REACTOR_WAKEUP_ERR;
    }
    uint64_t val;
    while(true) {
        auto ret = read(this->wake_fd_, &val, sizeof(val)); // 读取wake_fd，关闭事件，防止重复触发
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
        LOG_ERR("Reactor[%d] handle wake up err. errorno %d.", this->epoll_fd_, errno);
        return EVENT_STATUS::REACTOR_WAKEUP_ERR;
    }
    return EVENT_STATUS::OK;
}

EVENT_STATUS Reactor::do_pending_tasks()
{
    std::queue<reactor_task, std::deque<reactor_task>> pending_tasks;
    {
        std::lock_guard<std::mutex> lock(this->task_mutex_);
        pending_tasks.swap(this->pending_tasks_);
    }
    while (!pending_tasks.empty()) {
        pending_tasks.front()();
        pending_tasks.pop();
    }
    return EVENT_STATUS::OK;
}

bool Reactor::is_in_loop_thread()
{
    return std::this_thread::get_id() == this->reactor_thread_.get_id();
}

EVENT_STATUS Reactor::remove_all_conns()
{
    std::vector<int> fd_vec;
    fd_vec.reserve(connections_.size());
    for (const auto& conn : connections_) {
        fd_vec.push_back(conn.first);
    }
    for (const auto& fd : fd_vec) {
        reset_connection(fd);
    }
    return EVENT_STATUS::OK;
}

EVENT_STATUS Reactor::tcp_set_connection_state(const unsigned int& state, const int& fd) 
{
    epoll_event event;
    event.events = state;
    event.data.fd = fd;
    auto ret = epoll_ctl(this->epoll_fd_, EPOLL_CTL_MOD, fd, &event);
    if (ret == -1) {
        LOG_ERR("Reactor[%d] .", this->epoll_fd_);
        this->reset_connection(fd);
        return EVENT_STATUS::EPOLL_MOD_ERR;
    }
    return EVENT_STATUS::OK;
}