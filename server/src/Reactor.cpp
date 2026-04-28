#include <Reactor.h>
#include "EventHandler.h"
#include "Logger.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

void Reactor::reactor_running_thread() {
    epoll_event events[MAX_EVENTS];
    std::function<EVENT_STATUS(unsigned int)> task;
    LOG_INFO("Reactor[%d] running.", this->epoll_fd_);
    while (reactor_running_state_) {
        auto trig_times = epoll_wait(this->epoll_fd_, events, MAX_EVENTS, -1);
        for (int i = 0; i < trig_times; ++i) {
            if (events[i].data.fd == this->wake_fd_) {
                return;
            }
            std::shared_ptr<EventHandler> handler;
            {
                std::lock_guard<std::mutex> lock(conn_mutex_);
                auto find_iter = connections_.find(events[i].data.fd);
                if (find_iter == connections_.end() || find_iter->second == nullptr) { // at会抛出异常，此处用find
                    LOG_ERR("Reactor[%d] find handler[%d] err.", this->epoll_fd_, events[i].data.fd);
                    continue;
                }
                handler = find_iter->second;
            }
            auto state = events[i].events;
            thread_pool_->add_task([this, handler, state]() {
                return handler->handle_event(state);
            });
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
    epoll_ctl(this->epoll_fd_, EPOLL_CTL_ADD, this->wake_fd_, &event);
    LOG_INFO("Reactor[%d] initialization. Connection pool size %u.", epoll_fd_, conn_size_);

    // 挂接定时器，用于定时检测是否有长时间不活跃的客户端并定时清除
    // TimerHandler的构造函数会进行timerfd的创建和设置
    this->timer_handler_ = std::make_shared<TimerHandler>(this);
    auto add_ret = this->add_connection(this->timer_handler_->get_timer_fd(),
        EPOLLIN | EPOLLET, this->timer_handler_);
    LOG_INFO("[%s]Reactor[%d] add timer connection return %d.", __func__, this->epoll_fd_, add_ret);

    connections_.reserve(conn_size_);
    reactor_running_state_.store(true);
    reactor_thread_ = std::thread(&Reactor::reactor_running_thread, this);
}

void Reactor::reset_connection(const int& fd) {
    std::lock_guard<std::mutex> lock(conn_mutex_);
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
void Reactor::timer_reset_batch_conns(const std::vector<std::pair<int, unsigned int>>& expired_conns) {
    std::vector<int> actual_expired_conns;
    {
        std::lock_guard<std::mutex> lock(this->conn_mutex_);
        for (auto conn : expired_conns) {
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
    }
    for (auto conn : actual_expired_conns) {
        this->reset_connection(conn);
    }
}

Reactor::~Reactor() { // 担心不加锁有问题
    reactor_running_state_.store(false);
    uint64_t val = 1;   // 通过向wake_fd写入数据来唤醒epoll监听线程，此处应该读写8字节
    write(this->wake_fd_, &val, sizeof(val)); // 唤醒epoll监听线程
    if (reactor_thread_.joinable()) {
        reactor_thread_.join();
    }
    // 防止删除元素过程中迭代器失效，先把fd都取出来再挨个删除
    std::vector<int> release_conns;
    release_conns.reserve(this->connections_.size());
    for (auto conn_ : connections_) {
        release_conns.push_back(conn_.first);
    }
    for (auto conn : release_conns) {
        this->reset_connection(conn);
    }
    close(this->epoll_fd_);
    close(this->wake_fd_);
}

EVENT_STATUS Reactor::add_connection(const int& fd, unsigned int listen_state, std::shared_ptr<EventHandler> conn) {
    // 隐患：在加锁内进行数据结构的find操作，时间复杂度最坏可能达到O(n)，这里是一个将来高延迟的可疑点
    std::lock_guard<std::mutex> lock(conn_mutex_);
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

int Reactor::get_epoll_fd() const {
    return this->epoll_fd_;
}

void Reactor::add_timer_task(const int &fd, const unsigned int &version_no, const std::chrono::steady_clock::time_point &time_point) {
    this->timer_handler_->add_timer(fd, version_no, time_point);
}