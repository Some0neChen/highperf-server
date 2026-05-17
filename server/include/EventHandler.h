#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <unistd.h>
#include <vector>
#include "HttpContext.h"
#include "ServerPub.h"
#include "RequestBuffer.h"
#include "HttpModule.h"

class Reactor;
class HttpContext;

class EventHandler {
protected:
    int fd_;
public:
    EventHandler(int fd) : fd_(fd) {}
    virtual ~EventHandler();
    virtual EVENT_STATUS handle_event(unsigned int state) = 0;
};

/*
    socket_fd监听器处理类
*/
class ClientHandler : public EventHandler, public std::enable_shared_from_this<ClientHandler> {
public:
    ClientHandler(const int&,
        std::shared_ptr<Reactor>&);

    EVENT_STATUS handle_event(unsigned int state) override;
    EVENT_STATUS ClientHandleRequest();
    
    int get_fd() const;
    unsigned int get_version_no() const;
    // 连接过期时间更新，默认更新为当前时间加上固定的时间间隔，读取http报文和写回响应报文后刷新
    void update_expire_time();
    void update_expire_time(const std::chrono::steady_clock::time_point&);
    // http解析完成后的回调函数，交由上层处理，类里不持有线程池
    void queue_task_in_reactor(std::function<EVENT_STATUS()>);
    void on_request_ready(const size_t&, std::shared_ptr<RequestContent>&);
    // 响应准备好后的回调函数，在该Reactor线程中进行IO操作
    // 获取未写完的响应包，按序读写
    void on_response_ready();
    // 构造相应包时调用
    void on_response_ready(std::shared_ptr<OutgoingResponse>&);
    // 流量水线控制，当待发送数据过多时，暂不处理新的请求，等待已发送数据完成后再继续处理
    void apply_backpressure(unsigned int&);
private:
    // 客户端存放的是当前所受管理的Reactor
    // 因Reactor的Connection表中存放的EventHandler是ClientHandler，所以此处为避免循环引用，使用弱指针
    std::weak_ptr<Reactor> reactor_;
    std::shared_ptr<RequestBuffer<char>> buffer_; // 客户端请求读写缓冲区
    std::atomic<unsigned int> version_no;
    std::size_t pending_output_bytes_;
    HttpContext http_context_;
    bool is_blocking_;  // 是否处于TCP发送阻塞状态
    bool closed_wait_;
    unsigned int current_events_;

    EVENT_STATUS update_epoll_events(const unsigned int& state);
};

class ListenHandler : public EventHandler {
    int epoll_fd_;
    std::shared_ptr<std::vector<std::shared_ptr<Reactor>>> reactors_; // 服务端存储轮询备Reactor表
    unsigned int robin_count_; // 轮询计数
public:
    ListenHandler(const int&, int&,
        std::shared_ptr<std::vector<std::shared_ptr<Reactor>>>&);
    EVENT_STATUS handle_event(unsigned int state) override;
};

// 客户端请求任务包
struct HttpRequestTask {
    std::shared_ptr<RequestContent> request_header_;
    std::weak_ptr<ClientHandler> connetion_;
    size_t seq_no_;
    
    HttpRequestTask(const std::shared_ptr<RequestContent>& header, std::weak_ptr<ClientHandler> connection, const int& no) : 
        request_header_(header), connetion_(connection), seq_no_(no) {}
};

// 客户端连接对应处理任务时刻时间包，用来在reactor中记录每个链接的对应处理任务时的时间事件
struct TimeRecordPacket
{
    int fd;
    unsigned int version_no;
    std::chrono::steady_clock::time_point last_active_time;
};

// 时间包比较器，使最快过期的任务处于优先队列的堆顶
struct TimeRecordPacketComparator
{
    bool operator()(const TimeRecordPacket& lhs, const TimeRecordPacket& rhs) const {
        return lhs.last_active_time > rhs.last_active_time;
    }
};

class TimerHandler : public EventHandler {
    Reactor* reactor_;
    std::mutex timer_mutex_;
    std::priority_queue<TimeRecordPacket, std::vector<TimeRecordPacket>, TimeRecordPacketComparator> time_record_queue_;
public:
    TimerHandler(Reactor*);
    EVENT_STATUS handle_event(unsigned int state) override;
    void add_timer(const int& fd, const unsigned int& version_no, const std::chrono::steady_clock::time_point& active_time);
    int get_timer_fd() const;
};