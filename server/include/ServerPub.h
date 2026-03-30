/*
    webserver消息处理函数
*/
#pragma once
#include<memory>

enum EVENT_STATUS {
    OK = 0,
    ACCEPT_FD_ERR,
    EPOLL_ADD_ERR,
    EPOLL_EVENT_MISS,
    EPOLL_UNDEFINE_TRIG,
    CLIENT_READ_ERR,
    CLIENT_SEND_ERR,
    COUNT
};

struct TaskPacket;
class TaskManager;

EVENT_STATUS task_handle(std::shared_ptr<TaskPacket>); // 任务处理函数