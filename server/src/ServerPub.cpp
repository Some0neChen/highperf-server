#include "ServerPub.h"
#include "Logger.h"
#include "TaskPacket.h"
#include <cerrno>
#include <unistd.h>

void http_parse() {
    // TODO
}

EVENT_STATUS task_handle(std::shared_ptr<TaskPacket> task)
{
    task->buffer->at(task->len) = '\0';
    LOG_INFO("Client fd[%d] handle msg[%s] beginning", task->fd, task->buffer->data());
    http_parse(); // 假装有http解析器
    std::string reply("Recive OK. Msg: ");
    reply.append(task->buffer->data());
    int ret = write(task->fd, reply.data(), reply.size());
    if (ret <= 0) {
        LOG_INFO("Client fd[%d] reply err. errorno[%d].", task->fd, errno);
        return  EVENT_STATUS::CLIENT_SEND_ERR;
    }
    LOG_INFO("Client fd[%d] reply over. Expect replied len %d. Actually replied len %d.",
        task->fd, reply.size(), ret);
    return EVENT_STATUS::OK;
}