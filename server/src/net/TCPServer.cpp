#include "TCPServer.h"
#include "Acceptor.h"
#include "EventLoop.h"
#include "TCPConnection.h"
#include "Timer.h"
#include <functional>
#include <memory>
#include <utility>

void TCPServer::start(const char* ipaddr, const char* port)
{
    // 初始化主EventLoop
    main_loop_ = std::make_unique<EventLoop>();

    // 设定acceptor_用来监听TCPServer的地址及端口，将其挂接到主EventLoop中，并设置其后续接收到客户端连接后的分发函数
    acceptor_ = std::make_unique<Acceptor>();
    acceptor_->attach_socket(ipaddr, port);
    acceptor_->set_channel_dispatch_callback([this](const int& fd) {
        return channel_dispatch(fd);
    });
    auto loop = main_loop_.get();
    acceptor_->attach_loop(this->main_loop_.get());
    
    // 初始化定时器数组，每个定时器下标对应一个EventLoop
    robin_loop_timer_.reserve(TCPSERVER_SPEC::REACTOR_POOL_SIZE);
    robin_loop_timer_.resize(TCPSERVER_SPEC::REACTOR_POOL_SIZE);
    for (auto idx = 0; idx < TCPSERVER_SPEC::REACTOR_POOL_SIZE; ++idx) {
        robin_loop_timer_[idx] = std::make_shared<Timer>();
    }

    // 初始化EventLoop轮询数组
    robin_loop_.reserve(TCPSERVER_SPEC::REACTOR_POOL_SIZE);
    robin_loop_.resize(TCPSERVER_SPEC::REACTOR_POOL_SIZE);
    for (auto idx = 0; idx < TCPSERVER_SPEC::REACTOR_POOL_SIZE; ++idx) {
        robin_loop_[idx] = std::make_unique<EventLoop>();
        robin_loop_timer_[idx]->attach_loop(robin_loop_[idx].get());
    }

    // 初始化任务装载完毕，启动loop线程，刷新线程间同步状态
    // 看起来麻烦，实则避免TSAN上报读写channel状态未同步的无奈之举
    for (auto& loop : robin_loop_) {
        loop->start();
    }
    main_loop_->start();
    return;
}

TCPServer::~TCPServer() {
    /*  析构顺序
    *   1. 停止Acceptor，同步等待直到Acceptor资源完全释放，确保不再有新的连接进入
        2. 停止EventLoop线程池，停止监听事件，并防止后续还有事件调用主eventloop设置的TCPConnection退出notify回调，导致访问已析构的TCPConnection对象
        3. 停止mainLoop线程，此时TCPServer的处理都归属于主线程
        4. 后续的TCPConnection于定时器等资源的析构，在其析构函数里只保留fd的退出及删除，不再有线程及业务操作
     EventLoop 退出 loop
     EventLoop 析构
     wakeup fd / timerfd 析构
    */
    acceptor_->stop();
    robin_loop_.clear();
    main_loop_.reset();
    acceptor_.reset();
    robin_loop_timer_.clear();
    conn_manager_.clear();
}

void TCPServer::channel_remove(int conn_idx)
{
    main_loop_->queue_in_loop([this, conn_idx] () {
        auto conn = conn_manager_.find(conn_idx);
        if (conn == conn_manager_.end()) {
            return;
        }
        conn_manager_.erase(conn);
    });
}

void TCPServer::channel_dispatch(const int& fd)
{
    fcntl(fd , F_SETFL, O_NONBLOCK);
    auto selected_epoll_idx = robin_idx_ & TCPSERVER_SPEC::ROBIN_MASK;
    auto selected_loop = robin_loop_[selected_epoll_idx].get();
    auto selected_timer = robin_loop_timer_[selected_epoll_idx];
    auto [it, res] =  conn_manager_.try_emplace(this->robin_idx_, std::make_shared<TCPConnection>(fd, selected_loop));
    auto conn = it->second;
    if (!res) {
        return;
    }
    auto r_idx = robin_idx_;
    selected_loop->queue_in_loop([this, conn, selected_timer, r_idx]() {
        conn->set_timer_refresh_callback_([selected_timer](std::chrono::steady_clock::time_point time, std::function<void()> task)  {
            selected_timer->add_timer(time, std::move(task));
        });
        conn->set_close_notify([this, r_idx]() {
            this->channel_remove(r_idx);
        });
        upper_sync_create_callbackp(conn);
        conn->set_parse_ready_callback([this](std::weak_ptr<TCPConnection> conn) {
            this->parse_ready_callback_(conn);
        });
        conn->init_channel();
    });
    ++robin_idx_;
    return;
}

void TCPServer::set_upper_sync_create_callback(std::function<void(std::shared_ptr<TCPConnection>)> callbackFunc)
{
    upper_sync_create_callbackp = std::move(callbackFunc);
}

void TCPServer::set_parse_ready_callback(std::function<void(std::weak_ptr<TCPConnection>)> callbackFunc)
{
    parse_ready_callback_ = std::move(callbackFunc);
}