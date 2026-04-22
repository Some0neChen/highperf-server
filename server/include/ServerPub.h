/*
    webserver消息处理函数
*/
#pragma once
#include <memory>
#include <functional>

enum EVENT_STATUS {
    OK = 0,
    ACCEPT_FD_ERR,
    EPOLL_ADD_ERR,
    EPOLL_EVENT_MISS,
    EPOLL_UNDEFINE_TRIG,
    CLIENT_READ_ERR,
    CLIENT_SEND_ERR,
    CLIENT_USING_ERR,
    CLIENT_UNBIND_REACTOR_ERR,
    COUNT
};

enum SPECS_VALUE {
    FD_READ_SIZE = 2048,
    STANDARD_REQUEST_BUF_SIZE = 4096,   // 客户端TCP输入的标准请求缓冲区大小: 4KB
    HUG_MSG_BUFFER_SIZE = 64 * 1024,    // 读取客户端TCP输入的超大缓冲区临界值: 64KB
    WEB_SERVER_TIMER_INTERVAL = 20 * 60,      // webserver定时器唤醒间隔，秒级
};

struct TaskPacket;
class TaskManager;
class Reactor;
class EventHandler;
class ClientHandler;
template <typename T>
class ThreadPool;
class ClientHandler;
template <typename T>
class RequestBuffer;

EVENT_STATUS task_handle(std::shared_ptr<TaskPacket>); // 任务处理函数
int init_socket_fd(int argc, char* argv[]);
std::shared_ptr<Reactor> tcp_server_main_reactor_register(const int&,
    std::shared_ptr<ThreadPool<std::function<EVENT_STATUS()>>>&);