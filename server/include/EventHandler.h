#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <unistd.h>
#include <vector>
#include "ServerPub.h"
#include "RequestBuffer.h"
#include "HttpModule.h"

class Reactor;
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
class ClientHandler : public EventHandler {
public:
    ClientHandler(const int&,
        std::shared_ptr<Reactor>&);
    EVENT_STATUS handle_event(unsigned int state) override;
    auto get_version_no() const {
        return version_no.load();
    }
    void update_expire_time();
    void update_expire_time(const std::chrono::steady_clock::time_point&);
private:
    // 客户端存放的是当前所受管理的Reactor
    // 因Reactor的Connection表中存放的EventHandler是ClientHandler，所以此处为避免循环引用，使用弱指针
    std::weak_ptr<Reactor> reactor_;
    std::shared_ptr<RequestBuffer<char>> buffer_; // 客户端请求读写缓冲区
    std::atomic<unsigned int> version_no;
    RequestHandlerPacket request_packet_; // http报文请求包
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
struct TaskPacket {
    std::shared_ptr<RequestContent> request_header_;
    std::weak_ptr<Reactor> reactor_;
    int fd_;
    
    TaskPacket(const std::shared_ptr<RequestContent>& header, std::weak_ptr<Reactor>& reactor, const int& fd) : 
        request_header_(header), fd_(fd), reactor_(reactor) {}
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
    std::function<EVENT_STATUS(const int& fd)> timer_handler_func_;
    Reactor* reactor_;
    EVENT_STATUS timer_trigger_handler(const int&);
    std::mutex timer_mutex_;
    std::priority_queue<TimeRecordPacket,std::deque<TimeRecordPacket>, TimeRecordPacketComparator> time_record_queue_;
public:
    TimerHandler(Reactor*);
    EVENT_STATUS handle_event(unsigned int state) override;
    void add_timer(const int& fd, const unsigned int& version_no, const std::chrono::steady_clock::time_point& active_time);
    int get_timer_fd() const;
};