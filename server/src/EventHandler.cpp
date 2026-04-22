#include "EventHandler.h"
#include "HttpModule.h"
#include "Logger.h"
#include "ServerPub.h"
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#include "Reactor.h"

EventHandler::~EventHandler() {
    close(this->fd_);
}

ListenHandler::ListenHandler(const int& listen_fd, int& epoll,
    std::shared_ptr<std::vector<std::shared_ptr<Reactor>>>& reactors) :
    EventHandler(listen_fd), epoll_fd_(epoll), reactors_(reactors), robin_count_(0)
{
    if (fcntl(this->fd_, F_SETFL, O_NONBLOCK) < 0) {
        LOG_ERR("Server fd[%d] set O_NONBLOCK mode err.", this->fd_);
        exit(EXIT_FAILURE);
    }
}

EVENT_STATUS ListenHandler::handle_event(unsigned int state)
{
    struct sockaddr addr;
    socklen_t len = sizeof(sockaddr);

    if (!(state & EPOLLIN)) {
        LOG_ERR("Undefined epoll behavior. fd[%d].", this->fd_);
        return EVENT_STATUS::EPOLL_UNDEFINE_TRIG;
    }
    while (true) {
        auto client_fd = accept(this->fd_, &addr, &len);
        if (client_fd == -1) {
            if (errno == EINTR) {
                LOG_INFO("System interrupt listen fd[%d] to get client fd. errono[%d]", this->fd_, errno);
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                LOG_INFO("Listen fd[%d] accept client over.", this->fd_);
                break;;
            }
            LOG_INFO("Listen fd[%d] occur readding failed. errorno[%d]", this->fd_, errno);
            return EVENT_STATUS::CLIENT_READ_ERR;
        }
        sockaddr_in* client_addr = reinterpret_cast<sockaddr_in*>(&addr);
        LOG_INFO("get client fd:%d [%s:%u] connection.", client_fd, inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port));
        auto listen_state = EPOLLIN | EPOLLET | EPOLLRDHUP;
        ++this->robin_count_;
        auto reactor = this->reactors_->at(robin_count_ % this->reactors_->size());
        auto ret = reactor->
            add_connection(client_fd, listen_state, std::make_shared<ClientHandler>(client_fd, reactor));
        if (ret != EVENT_STATUS::OK) {
            LOG_ERR("epoll add client_fd err.");
            return ret;  
        }
    }
    return EVENT_STATUS::OK;;
}

ClientHandler::ClientHandler(const int& fd, std::shared_ptr<Reactor>& reactor)
    : EventHandler(fd),
      reactor_(reactor),
      buffer_(std::make_shared<RequestBuffer<char>>()),
      request_packet_(this->buffer_),
      version_no(0)
{
    if (fcntl(this->fd_, F_SETFL, O_NONBLOCK) < 0) {
        LOG_ERR("Client fd[%d] set O_NONBLOCK mode err.", this->fd_);
    }
    this->update_expire_time();
}

EVENT_STATUS ClientHandler::handle_event(unsigned int state)
{
    LOG_INFO("Client fd[%d] readding begain.", this->fd_);
    auto reactor_ptr = this->reactor_.lock();
    if (reactor_ptr == nullptr) {
        LOG_ERR("Client fd[%d] get reactor failed. The reactor is already released.", this->fd_);
        return EVENT_STATUS::CLIENT_UNBIND_REACTOR_ERR;
    }
    if (state & EPOLLRDHUP) { // 客户端退出
        LOG_INFO("Client fd[%d] closed connection.", this->fd_);
        reactor_ptr->reset_connection(this->fd_);
        return EVENT_STATUS::OK;
    }
    if (!(state & EPOLLIN)) { // 不处理非读事件
        LOG_ERR("Undefined epoll behavior. fd[%d].", this->fd_);
        return EVENT_STATUS::EPOLL_UNDEFINE_TRIG;
    }
    while (true) {
        // 采用readv分散读的方式，先将数据读入缓冲区剩余空间，如果数据长度超过剩余空间，则将溢出部分读入extra_buf中
        iovec read_vec[2];
        read_vec[0].iov_base = this->buffer_->get_data() + this->buffer_->get_write_pos();
        read_vec[0].iov_len = this->buffer_->writable_size();
        char extra_buf[65536];
        read_vec[1].iov_base = extra_buf;
        read_vec[1].iov_len = sizeof(extra_buf);
        // len不能用size_t，因为size_t本质是无符号数，没有-1
        ssize_t len = readv(this->fd_, read_vec, 2); 

        // 客户端退出
        if (len == 0) {
            LOG_INFO("Client fd[%d] closed connection.", this->fd_);
            reactor_ptr->reset_connection(this->fd_);
            return EVENT_STATUS::OK;
        }

        // 已读完或者发生错误
        if (len == -1) {
            if (errno == EINTR) {
                LOG_INFO("System interrupt client fd[%d] reading. errono[%d]", this->fd_, errno);
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                LOG_INFO("Client fd[%d] readding over.", this->fd_);
                break;
            }
            LOG_INFO("Client fd[%d] occur readding failed. errorno[%d]", this->fd_, errno);
            return EVENT_STATUS::CLIENT_READ_ERR;
        }

        // 更新缓冲区写入位置
        buffer_->update_write_pos(len, extra_buf);
        LOG_INFO("Epoll[%d] Client fd[%d] receive msg success, len %u", reactor_ptr->get_epoll_fd(), this->fd_, len);
    }
    // 测试用，打印报文长啥样：
    // std::string http(this->buffer_->get_data() + this->buffer_->get_read_pos(), this->buffer_->readable_size());
    // LOG_INFO("--------------------------------\n%s\n--------------------------------", http.c_str());
    //--
    while (HttpFsmManager::get_fsm().fsm_excute( this->request_packet_) == ParseResult::COMPLETE) {
        // 构造任务包
        auto request_header = this->request_packet_.pop_content();
        std::shared_ptr<TaskPacket> packet = std::make_shared<TaskPacket>(request_header, this->fd_);
        reactor_ptr->thread_pool_->add_task([packet, this](){
            LOG_INFO("fd[%d] push task into http thread queue.", packet->fd);
            return this->task_handle(packet);
        });
    }
    this->update_expire_time();
    return EVENT_STATUS::OK;
}

void ClientHandler::update_expire_time() {
    this->version_no.fetch_add(1);
    auto expired_time = std::chrono::steady_clock::now() + std::chrono::seconds(SPECS_VALUE::WEB_SERVER_TIMER_INTERVAL);
    this->reactor_.lock()->add_timer_task(this->fd_, this->get_version_no(), expired_time);
}

void ClientHandler::update_expire_time(const std::chrono::steady_clock::time_point& time_point) {
    this->version_no.fetch_add(1);
    this->reactor_.lock()->add_timer_task(this->fd_, this->get_version_no(), time_point);
}

TimerHandler::TimerHandler(Reactor* reactor)
        : EventHandler(0), reactor_(reactor)
{
    if (reactor == nullptr) {
        LOG_ERR("TimerManager get shared reactor err.");
        exit(EXIT_FAILURE);
    }

    // 设置定时器
    itimerspec ts = { 0 };
    // 如果设置成tv_nsec，则为毫秒级，且同时需要将tv_sec设置为0，否则会被当成秒级，导致定时器间隔过长
    // 如果设置为0，则代表不启动定时器，除非后续通过timerfd_settime来设置定时器初始值，否则定时器将永远不会触发
    ts.it_interval.tv_sec = SPECS_VALUE::WEB_SERVER_TIMER_INTERVAL; // 定时器间隔时间，单位秒
    ts.it_value.tv_sec = SPECS_VALUE::WEB_SERVER_TIMER_INTERVAL;    // 定时器初始值，单位秒
    this->fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (this->fd_ == -1) {
        LOG_ERR("Reactor[%d] create timerfd err. errorno %d.", reactor_->epoll_fd_, errno);
        exit(EXIT_FAILURE);
    }
    auto ret = timerfd_settime(this->fd_, 0, &ts, nullptr);
    if (ret == -1) {
        LOG_ERR("Reactor[%d] set timerfd time err. errorno %d.", reactor_->epoll_fd_, errno);
        exit(EXIT_FAILURE);
    }
    LOG_INFO("[%s]Reactor[%d] create and set timerfd[%d] successfully.", __func__, reactor_->epoll_fd_, this->fd_);
}

void TimerHandler::add_timer(const int& fd, const unsigned int& version_no, const std::chrono::steady_clock::time_point& active_time) {
    std::lock_guard<std::mutex> lock(timer_mutex_);
    this->time_record_queue_.push(TimeRecordPacket{fd, version_no, active_time});
}

int TimerHandler::get_timer_fd() const {
        return this->fd_;
}

EVENT_STATUS TimerHandler::handle_event(unsigned int state) {
    // 错误信号处理
    LOG_INFO("Client fd[%d] readding begain.", this->fd_);
    if (this->reactor_ == nullptr) {
        LOG_ERR("Client fd[%d] get reactor failed. The reactor is already released.", this->fd_);
        return EVENT_STATUS::CLIENT_UNBIND_REACTOR_ERR;
    }
    if (!(state & EPOLLIN)) { // 不处理非读事件
        LOG_ERR("Undefined epoll timer behavior. fd[%d].", this->fd_);
        return EVENT_STATUS::EPOLL_UNDEFINE_TRIG;
    }

    // 处理定时器事件
    LOG_INFO("[%s] Reactor[%d] timer trigger handler called.", __func__, this->reactor_->epoll_fd_);
    uint64_t trig_times;
    auto ret = read(this->fd_, &trig_times, sizeof(uint64_t));
    if (ret == -1) {
        LOG_ERR("[%s] Reactor[%d] read timerfd err. errorno %d.", __func__, this->reactor_->epoll_fd_, errno);
        exit(EXIT_FAILURE);
    }

    // 批量取出已经过时的时间事件，这么处理是为了可以少反复加锁，在Reactor也可以只加锁一次后批量处理
    std::vector<std::pair<int, unsigned int>> expired_conns;
    {
        std::lock_guard<std::mutex> lock(timer_mutex_);
        auto now_time = std::chrono::steady_clock::now();
        if (this->time_record_queue_.empty() || now_time < this->time_record_queue_.top().last_active_time) {
            return EVENT_STATUS::OK;
        }
        while (!this->time_record_queue_.empty() && now_time > this->time_record_queue_.top().last_active_time) {
            expired_conns.emplace_back(this->time_record_queue_.top().fd, this->time_record_queue_.top().version_no);
            this->time_record_queue_.pop();
        }
    }
    this->reactor_->timer_reset_batch_conns(expired_conns);
    return EVENT_STATUS::OK;
}