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
class BufferPool;

EVENT_STATUS task_handle(std::shared_ptr<TaskPacket>); // 任务处理函数
int init_socket_fd(int argc, char* argv[]);
std::shared_ptr<Reactor> tcp_server_main_reactor_register(const int&,
    std::shared_ptr<ThreadPool<std::function<EVENT_STATUS()>>>&,
    std::shared_ptr<BufferPool<std::array<char, SPECS_VALUE::FD_READ_SIZE>>>&);